/*
 * Chapter 10, Step 2 — the image the server will serve:
 * v1.3.0, packaged as a .zigbee file.
 *
 * Code-identical to Step 1's FOTA client except for three
 * deliberate, visible deltas — because this step's subject
 * is not the client but the PACKAGE and the SERVER:
 *
 *   - Version 1.3.0 (both stamps together, as always). The
 *     build's DFU multi-image machinery wraps the signed
 *     image into a .zigbee OTA file whose header carries
 *     manufacturer 0x127F, image type 0x0141, and file
 *     version 0x01030000 — the imgtool stamp, BCD-packed.
 *     Step 1's build produced file version 0x01020000 the
 *     same way; the pair of packages is the lab's before
 *     and after.
 *   - A canary: CONFIG_OCCUPANCY_OFF_DELAY_SEC drops to
 *     10 s in prj.conf, so a successful update is provable
 *     by BEHAVIOUR at the desk (the bulb obeys the new
 *     delay), not just by a version string.
 *   - A policy gate switched on: the client's minimum
 *     hardware-version check (see prj.conf). Version policy
 *     in OTA is three gates — server offers, client HW
 *     window, bootloader signature — and this step makes
 *     each one explicit.
 *
 * The serving procedure itself (a spare DK flashed with the
 * add-on's NCP sample, nrfutil's dfu command serving the
 * .zigbee file) lives in this step's README: the server
 * side of Zigbee OTA is a host tool and a radio, not
 * firmware you write.
 *
 * (What follows is Chapter 8 Step 2's own header — the
 * firmware it describes is the firmware in this file.)
 *
 * Binding & actuation: occupancy switches the bulb.
 *
 * The endpoint gains an On/Off CLIENT cluster (see the header),
 * and the glue gains a second output channel:
 *
 *   - REPORTING (Step 1) tells subscribers what the attribute
 *     IS — the coordinator/hub's channel.
 *   - COMMANDS (this step) tell the bulb what to DO — sent from
 *     our On/Off client to the bulb's On/Off server, addressed
 *     through the BINDING TABLE: the send below names no device
 *     at all. The coordinator creates the binding in the lab
 *     (`zdo bind`), the APS layer resolves it at send time.
 *     Firmware that knows no addresses can be re-aimed from a
 *     shell; that is the entire architectural point of binding.
 *
 * The actuation policy is a state machine with a memory:
 *
 *   occupied     -> cancel any pending Off; if the bulb was not
 *                   last commanded On, send On.
 *   unoccupied   -> start the hold timer
 *                   (CONFIG_OCCUPANCY_OFF_DELAY_SEC, app Kconfig):
 *                   the room must read empty CONTINUOUSLY for
 *                   the delay before Off is sent. This is the
 *                   ZCL delay-attribute policy (the spec's
 *                   Ultrasonic occupied-to-unoccupied delay,
 *                   0x0020) implemented in the glue, because
 *                   ZBOSS's standard attribute list does not
 *                   declare the delay attributes.
 *
 * On/Off commands are idempotent, and "last commanded" is the
 * app's own bookkeeping: after a reboot the state is UNKNOWN,
 * and the first transition always sends — one redundant command
 * is the honest price of not pretending to know.
 *
 * The sensor pipeline below is Chapter 7/8.1's, unchanged.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <app_version.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>
#include <dk_buttons_and_leds.h>

#include <zboss_api.h>
#include <zigbee/zigbee_fota.h>
#include <zigbee/zigbee_error_handler.h>
#include <zigbee/zigbee_app_utils.h>
#include <zb_nrf_platform.h>
#include "zb_occupancy_sensor.h"

#include "ld2410.h"


/* Device endpoint — the same number Chapters 6–7 use throughout. */
#define SENSOR_ENDPOINT                     10

/* Type of power sources available for the device.
 * For possible values see section 3.2.2.2.8 of ZCL specification.
 */
#define SENSOR_INIT_BASIC_POWER_SOURCE      ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE

/* LED indicating that device successfully joined Zigbee network. */
#define ZIGBEE_NETWORK_STATE_LED            DK_LED3

/* LED used for device identification. */
#define IDENTIFY_LED                        DK_LED4

/* Button used to enter the Identify mode. */
#define IDENTIFY_MODE_BUTTON                DK_BTN4_MSK

/* A long press of the same button is the factory reset. */
#define FACTORY_RESET_BUTTON                IDENTIFY_MODE_BUTTON

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* Application context: the attribute storage behind the endpoint's
 * clusters. ZBOSS reads and writes these structs; the application
 * owns them.
 */
struct zb_device_ctx {
	zb_zcl_basic_attrs_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
	zb_uint8_t occupancy;
	zb_uint8_t occupancy_sensor_type;
	zb_uint8_t occupancy_sensor_type_bitmap;
};

/* Zigbee device application context storage. */
static struct zb_device_ctx dev_ctx;

ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(
	identify_attr_list,
	&dev_ctx.identify_attr.identify_time);

ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST(
	basic_attr_list,
	&dev_ctx.basic_attr.zcl_version,
	&dev_ctx.basic_attr.power_source);

ZB_ZCL_DECLARE_OCCUPANCY_SENSING_ATTRIB_LIST(
	occupancy_attr_list,
	&dev_ctx.occupancy,
	&dev_ctx.occupancy_sensor_type,
	&dev_ctx.occupancy_sensor_type_bitmap);

ZB_DECLARE_OCCUPANCY_SENSOR_CLUSTER_LIST(
	sensor_clusters,
	basic_attr_list,
	identify_attr_list,
	occupancy_attr_list);

ZB_DECLARE_OCCUPANCY_SENSOR_EP(
	sensor_ep,
	SENSOR_ENDPOINT,
	sensor_clusters);

/* Chapter 10: the device context gains the FOTA library's
 * endpoint. The library owns and defines zigbee_fota_client_ep
 * (the OTA Upgrade client cluster lives on it); the app only
 * registers it, FIRST in the list, next to its own endpoint —
 * the same shape as the add-on's FOTA sample. The endpoints
 * must differ: ours is 10, the library's is 5 (prj.conf).
 */
extern zb_af_endpoint_desc_t zigbee_fota_client_ep;

ZBOSS_DECLARE_DEVICE_CTX_2_EP(
	sensor_ctx,
	zigbee_fota_client_ep,
	sensor_ep);


/**@brief Function for initializing all clusters attributes. */
static void app_clusters_attr_init(void)
{
	/* Basic cluster attributes data */
	dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	dev_ctx.basic_attr.power_source = SENSOR_INIT_BASIC_POWER_SOURCE;

	/* Identify cluster attributes data. */
	dev_ctx.identify_attr.identify_time =
		ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

	/* Occupancy Sensing attributes — see Step 1 for why an
	 * mmWave radar honestly reports sensor type ULTRASONIC:
	 * the ZCL enum has no radar value, and an active wave
	 * sensor belongs to the ultrasonic family, not to PIR.
	 */
	dev_ctx.occupancy =
		ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_UNOCCUPIED;
	dev_ctx.occupancy_sensor_type =
		ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_ULTRASONIC;
	dev_ctx.occupancy_sensor_type_bitmap =
		ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_BITMAP_ULTRASONIC;
}

/**@brief Publish a new Occupancy value through the stack.
 *
 * Runs in ZBOSS scheduler context. ZB_ZCL_SET_ATTRIBUTE stores
 * the value in dev_ctx — and, new in Chapter 8, the stack's
 * reporting engine sees the write: if a coordinator configured
 * reporting for this attribute, a change that passes the
 * reportable-change test is emitted as a Report Attributes
 * command (subject to the min/max intervals), with no further
 * code here. The writer writes; the engine reports.
 *
 * @param  bufid  The new occupancy value (0 or 1), riding the
 *                scheduler's parameter slot.
 */
static void publish_occupancy(zb_bufid_t bufid)
{
	zb_uint8_t occupied = (zb_uint8_t)bufid;

	ZB_ZCL_SET_ATTRIBUTE(
		SENSOR_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
		&occupied,
		ZB_FALSE);

	LOG_INF("Occupancy attribute -> %s",
		occupied ? "OCCUPIED" : "UNOCCUPIED");
}

/* ------------------------------------------------------------------ */
/* The sensor glue — driver readings become attribute writes.          */
/* ------------------------------------------------------------------ */

static const struct device *const sensor_uart =
	DEVICE_DT_GET(DT_NODELABEL(uart1));

static struct k_work publish_work;

/* ------------------------------------------------------------------ */
/* Actuation — occupancy becomes On/Off commands to the bound bulb.    */
/* ------------------------------------------------------------------ */

/* What we last ASKED the bulb to be. Not what it IS: a binding
 * delivers commands, not knowledge — after a reboot, or if the
 * bulb was switched by someone else, this is a belief, and the
 * design treats it as one (idempotent commands, resend on every
 * transition into a state we haven't commanded).
 */
enum bulb_belief {
	BULB_UNKNOWN = 0,
	BULB_COMMANDED_ON,
	BULB_COMMANDED_OFF,
};

static enum bulb_belief bulb_belief = BULB_UNKNOWN;

/* The hold timer: armed when the room empties, cancelled when
 * it fills. Its expiry is the only path to an Off command.
 */
static struct k_work_delayable off_delay_work;
static bool off_pending;
static int64_t off_deadline_ms;

/**@brief Send an On or Off command to the bound bulb endpoint(s).
 *
 * Runs in ZBOSS context (the buffer arrives via
 * zb_buf_get_out_delayed_ext, the stack's own hand-off). The
 * destination is deliberately NOT named: with
 * ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT, APS resolves it
 * from this endpoint's binding table entries for the On/Off
 * cluster — the entries the coordinator's `zdo bind` created.
 * No entry, no delivery: the stack drops the packet, which is
 * why the lab binds BEFORE it tests, and why Chapter 11 can
 * watch the difference.
 */
static void send_on_off(zb_bufid_t bufid, zb_uint16_t cmd_id)
{
	/* The SEND_REQ macros take the destination address BY
	 * REFERENCE (&(addr) inside ZB_ZCL_SEND_COMMAND_SHORT), so
	 * a literal cannot be passed — a variable is required even
	 * though DST_ADDR_ENDP_NOT_PRESENT never reads its value.
	 * (The add-on's light_switch passes its stored bulb address
	 * the same way; ours is a named zero, because the binding
	 * table, not this variable, is the address book.)
	 */
	zb_uint16_t addr_from_binding = 0;

	if (cmd_id == ZB_ZCL_CMD_ON_OFF_ON_ID) {
		ZB_ZCL_ON_OFF_SEND_ON_REQ(
			bufid,
			addr_from_binding,
			ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
			0,
			SENSOR_ENDPOINT,
			ZB_AF_HA_PROFILE_ID,
			ZB_ZCL_DISABLE_DEFAULT_RESPONSE,
			NULL);
		LOG_INF("On/Off: sent ON via binding");
	} else {
		ZB_ZCL_ON_OFF_SEND_OFF_REQ(
			bufid,
			addr_from_binding,
			ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
			0,
			SENSOR_ENDPOINT,
			ZB_AF_HA_PROFILE_ID,
			ZB_ZCL_DISABLE_DEFAULT_RESPONSE,
			NULL);
		LOG_INF("On/Off: sent OFF via binding");
	}
}

/**@brief Ask for a command to be sent (any thread context).
 *
 * Sending needs a stack buffer and stack context; the delayed-
 * buffer hand-off is the stack's front door for exactly that
 * (the add-on's light_switch sample knocks the same way).
 */
static void request_bulb(bool on)
{
	if (!ZB_JOINED()) {
		/* Not a member: there is no binding table worth
		 * the name and nowhere for a packet to go. Say so,
		 * keep the belief UNKNOWN, and let the next
		 * transition retry — honesty over bookkeeping.
		 */
		LOG_WRN("On/Off: not joined - %s command skipped",
			on ? "ON" : "OFF");
		return;
	}

	zb_buf_get_out_delayed_ext(send_on_off,
				   on ? ZB_ZCL_CMD_ON_OFF_ON_ID :
					ZB_ZCL_CMD_ON_OFF_OFF_ID,
				   0);
	bulb_belief = on ? BULB_COMMANDED_ON : BULB_COMMANDED_OFF;
}

/* Hold-timer expiry (workqueue context): the room has read
 * empty for the whole delay. If it still does, the bulb's turn
 * is over. The state check is not paranoia — a cancellation can
 * race an already-queued handler, and the handler must believe
 * the present, not the schedule.
 */
static bool last_occupied;

static void off_delay_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	off_pending = false;

	if (!last_occupied) {
		request_bulb(false);
	}
}

/* Workqueue context: re-read the FRESHEST snapshot (a burst of
 * frames between callback and work collapses into one truthful
 * write), derive the boolean, hand the attribute write to the
 * stack's scheduler — and run the actuation state machine:
 * two consumers (attribute, bulb), one transition, one place
 * where both are decided.
 */
static void publish_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct ld2410_reading r;

	if (!ld2410_latest(&r)) {
		return;
	}

	bool occupied = (r.target != LD2410_TARGET_NONE);

	zb_uint8_t attr = occupied ?
		ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_OCCUPIED :
		ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_UNOCCUPIED;

	ZB_SCHEDULE_APP_CALLBACK(publish_occupancy, attr);

	if (occupied) {
		/* Presence wins instantly — nobody waits for a
		 * light. Cancel any pending Off and light the room
		 * unless we already asked for exactly that.
		 */
		if (off_pending) {
			k_work_cancel_delayable(&off_delay_work);
			off_pending = false;
		}
		if (bulb_belief != BULB_COMMANDED_ON) {
			request_bulb(true);
		}
	} else {
		/* Absence must prove itself over time: arm the
		 * hold timer instead of switching off on a single
		 * empty reading. The attribute, note, reports the
		 * emptiness immediately — reporting tells the
		 * truth fast; actuation moves slowly on purpose.
		 */
		if (bulb_belief != BULB_COMMANDED_OFF && !off_pending) {
			off_pending = true;
			off_deadline_ms = k_uptime_get() +
				CONFIG_OCCUPANCY_OFF_DELAY_SEC * 1000LL;
			k_work_schedule(&off_delay_work,
					K_SECONDS(CONFIG_OCCUPANCY_OFF_DELAY_SEC));
			LOG_INF("On/Off: room empty - OFF in %d s unless presence returns",
				CONFIG_OCCUPANCY_OFF_DELAY_SEC);
		}
	}
}

/* Driver callback — parser-thread context, so: filter + defer,
 * exactly the contract ld2410.h states. The attribute is a
 * boolean, so the only event worth forwarding is a TRANSITION:
 * target appeared (NONE -> anything) or vanished (anything ->
 * NONE). Distance wiggles inside a state are Chapter 4's
 * deadband problem; a boolean has nothing to wiggle.
 * (last_occupied itself is declared with the actuation state
 * above — the hold-timer handler reads it too.)
 */
static void on_reading(const struct ld2410_reading *r, void *user_data)
{
	ARG_UNUSED(user_data);

	bool occupied = (r->target != LD2410_TARGET_NONE);

	if (occupied != last_occupied) {
		last_occupied = occupied;
		k_work_submit(&publish_work);
	}
}

/* ------------------------------------------------------------------ */
/* The shell command — Chapter 4's `presence`, one line richer:        */
/* it now also shows what the NETWORK believes (the attribute).         */
/* ------------------------------------------------------------------ */

static const char *const target_words[] = {
	"nobody", "moving", "stationary", "moving + stationary"
};

static int cmd_presence(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct ld2410_reading r;
	struct ld2410_stats s;

	ld2410_stats(&s);

	if (!ld2410_latest(&r)) {
		shell_print(sh, "presence : no valid frame received yet");
	} else if (r.target == LD2410_TARGET_NONE) {
		shell_print(sh, "presence : no");
		shell_print(sh, "distance : —");
	} else {
		shell_print(sh, "presence : YES (%s)", target_words[r.target]);
		shell_print(sh, "distance : %u cm",
			    ld2410_headline_distance_cm(&r));
	}

	shell_print(sh, "attribute: Occupancy = %u (%s)",
		    dev_ctx.occupancy,
		    dev_ctx.occupancy ? "OCCUPIED" : "UNOCCUPIED");
	shell_print(sh, "bulb     : last commanded %s",
		    bulb_belief == BULB_COMMANDED_ON ? "ON" :
		    bulb_belief == BULB_COMMANDED_OFF ? "OFF" : "UNKNOWN");
	if (off_pending) {
		int64_t left_ms = off_deadline_ms - k_uptime_get();

		shell_print(sh, "off timer: OFF in %lld s unless presence returns",
			    left_ms > 0 ? left_ms / 1000 : 0);
	}
	shell_print(sh, "frames   : %u parsed, %u resyncs, %u dropped bytes, "
		    "%u engineering frames",
		    s.frames_ok, s.resyncs, s.rx_dropped, s.eng_frames);

	return 0;
}

SHELL_CMD_REGISTER(presence, NULL,
		   "Show live LD2410 presence, driver statistics, the Occupancy attribute, and bulb state",
		   cmd_presence);

/**@brief Function to toggle the identify LED
 *
 * @param  bufid  Unused parameter, required by ZBOSS scheduler API.
 */
static void toggle_identify_led(zb_bufid_t bufid)
{
	static int blink_status;

	dk_set_led(IDENTIFY_LED, (++blink_status) % 2);
	ZB_SCHEDULE_APP_ALARM(toggle_identify_led, bufid, ZB_MILLISECONDS_TO_BEACON_INTERVAL(100));
}

/**@brief Function to handle identify notification events on the endpoint.
 *
 * @param  bufid  Unused parameter, required by ZBOSS scheduler API.
 */
static void identify_cb(zb_bufid_t bufid)
{
	zb_ret_t zb_err_code;

	if (bufid) {
		/* Schedule a self-scheduling function that will toggle the LED */
		ZB_SCHEDULE_APP_CALLBACK(toggle_identify_led, bufid);
	} else {
		/* Cancel the toggling function alarm and turn off LED */
		zb_err_code = ZB_SCHEDULE_APP_ALARM_CANCEL(toggle_identify_led, ZB_ALARM_ANY_PARAM);
		ZVUNUSED(zb_err_code);

		dk_set_led(IDENTIFY_LED, 0);
	}
}

/**@brief Starts identifying the device.
 *
 * @param  bufid  Unused parameter, required by ZBOSS scheduler API.
 */
static void start_identifying(zb_bufid_t bufid)
{
	ZVUNUSED(bufid);

	if (ZB_JOINED()) {
		if (dev_ctx.identify_attr.identify_time ==
		ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE) {

			zb_ret_t zb_err_code = zb_bdb_finding_binding_target(
				SENSOR_ENDPOINT);

			if (zb_err_code == RET_OK) {
				LOG_INF("Enter identify mode");
			} else if (zb_err_code == RET_INVALID_STATE) {
				LOG_WRN("RET_INVALID_STATE - Cannot enter identify mode");
			} else {
				ZB_ERROR_CHECK(zb_err_code);
			}
		} else {
			LOG_INF("Cancel identify mode");
			zb_bdb_finding_binding_target_cancel();
		}
	} else {
		LOG_WRN("Device not in a network - cannot enter identify mode");
	}
}

/**@brief Callback for button events.
 *
 * Button 1's simulation job is done — the sensor owns occupancy
 * now. Button 4 keeps its Chapter 6 duties (identify / reset).
 */
static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if (IDENTIFY_MODE_BUTTON & has_changed) {
		if (IDENTIFY_MODE_BUTTON & button_state) {
			/* Button changed its state to pressed */
		} else {
			/* Button changed its state to released */
			if (was_factory_reset_done()) {
				/* The long press was for Factory Reset */
				LOG_DBG("After Factory Reset - ignore button release");
			} else   {
				/* Button released before Factory Reset */

				/* Start identification mode */
				ZB_SCHEDULE_APP_CALLBACK(start_identifying, 0);
			}
		}
	}

	check_factory_reset_button(button_state, has_changed);
}

/**@brief Function for initializing LEDs and Buttons. */
static void configure_gpio(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		LOG_ERR("Cannot init buttons (err: %d)", err);
	}

	err = dk_leds_init();
	if (err) {
		LOG_ERR("Cannot init LEDs (err: %d)", err);
	}
}

/**@brief Zigbee stack event handler — Chapter 6's, plus
 * Chapter 9's one product behaviour: the image trial.
 *
 * A swapped-in image runs on parole. This product's parole
 * condition is membership: the first signal that finds the
 * device JOINED — stack started, steering done, radio path
 * proven end to end — the image confirms itself. (The
 * add-on's own FOTA sample confirms on the same class of
 * signal; the policy being taught is "confirm on evidence
 * the product itself defines as healthy", not "confirm
 * early and hope".) The flag makes it once-per-boot; the
 * bootloader makes it once-ever.
 */
/* ------------------------------------------------------------------ */
/* Chapter 10: FOTA glue. The library does the protocol; the app     */
/* owns the POLICY — what to log, and what to do when an image has   */
/* fully arrived. Mirrors the add-on light_switch FOTA sample's      */
/* glue, adapted to this product (no spare LED to blink: progress    */
/* goes to the log, where a student can count it).                   */
/* ------------------------------------------------------------------ */

static void ota_evt_handler(const struct zigbee_fota_evt *evt)
{
	switch (evt->id) {
	case ZIGBEE_FOTA_EVT_PROGRESS:
		LOG_INF("OTA download: %d%%", evt->dl.progress);
		break;

	case ZIGBEE_FOTA_EVT_FINISHED:
		/* The image is complete in slot 1 and marked for
		 * a test swap by the DFU target. Rebooting hands
		 * over to MCUboot — and to the trial whose verdict
		 * this same file's confirm-on-join policy decides.
		 */
		LOG_INF("OTA download complete - rebooting into "
			"the new image's trial");
		sys_reboot(SYS_REBOOT_COLD);
		break;

	case ZIGBEE_FOTA_EVT_ERROR:
		LOG_ERR("OTA download failed - staying on the "
			"current image");
		break;

	default:
		break;
	}
}

/* ZCL device callback: the OTA cluster's commands do not
 * arrive as cluster callbacks; ZBOSS surfaces them as a
 * device-callback ID, and the app must route them to the
 * library. Anything else is not implemented here.
 */
static void zcl_device_cb(zb_bufid_t bufid)
{
	zb_zcl_device_callback_param_t *device_cb_param =
		ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);

	if (device_cb_param->device_cb_id ==
	    ZB_ZCL_OTA_UPGRADE_VALUE_CB_ID) {
		zigbee_fota_zcl_cb(bufid);
	} else {
		device_cb_param->status = RET_NOT_IMPLEMENTED;
	}
}

static bool image_confirmed;
void zboss_signal_handler(zb_bufid_t bufid)
{
	/* Chapter 10: the FOTA library tracks stack signals
	 * (it learns about servers and schedules its queries
	 * from them) — every signal is forwarded, first.
	 */
	zigbee_fota_signal_handler(bufid);

	/* Update network status LED. */
	zigbee_led_status_update(bufid, ZIGBEE_NETWORK_STATE_LED);

	/* No application-specific behavior is required.
	 * Call default signal handler.
	 */
	ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));

	if (!image_confirmed && ZB_JOINED()) {
		int err = boot_write_img_confirmed();

		if (err == 0) {
			LOG_INF("Firmware v%s: joined and healthy - "
				"image CONFIRMED (permanent)",
				APP_VERSION_STRING);
		} else {
			LOG_WRN("boot_write_img_confirmed() failed (%d) - "
				"this image is still on trial", err);
		}
		image_confirmed = true;
	}

	/* All callbacks should either reuse or free passed buffers.
	 * If bufid == 0, the buffer is invalid (not passed).
	 */
	if (bufid) {
		zb_buf_free(bufid);
	}
}

int main(void)
{
	LOG_INF("Starting Zigbee, Engineered - occupancy product, firmware v%s",
		APP_VERSION_STRING);

	/* Initialize */
	configure_gpio();
	register_factory_reset_button(FACTORY_RESET_BUTTON);

	/* Chapter 10: start the FOTA client and register the
	 * ZCL device callback that routes OTA commands to it —
	 * BEFORE the device context goes live, the same order
	 * the add-on's FOTA sample uses.
	 */
	zigbee_fota_init(ota_evt_handler);
	ZB_ZCL_REGISTER_DEVICE_CB(zcl_device_cb);

	/* Register device context (endpoints). */
	ZB_AF_REGISTER_DEVICE_CTX(&sensor_ctx);

	app_clusters_attr_init();

	/* Register handlers to identify notifications */
	ZB_AF_SET_IDENTIFY_NOTIFICATION_HANDLER(SENSOR_ENDPOINT, identify_cb);

	/* Start Chapter 4's driver on uart1 — the same call, the
	 * same contract as in its own chapter. A failed start is
	 * loud but not fatal to the Zigbee side: the endpoint still
	 * joins and answers (with a permanently unoccupied
	 * attribute), and the log says exactly why.
	 */
	k_work_init(&publish_work, publish_handler);
	k_work_init_delayable(&off_delay_work, off_delay_handler);

	int err = ld2410_start(sensor_uart, on_reading, NULL);

	if (err != 0) {
		LOG_ERR("LD2410 driver failed to start (%d) - "
			"endpoint will report UNOCCUPIED; check the overlay/wiring",
			err);
	}

	/* Start Zigbee default thread */
	zigbee_enable();

	LOG_INF("Endpoint 10 live: Occupancy drives the bound bulb "
		"(OFF delay %d s). Bind us to the bulb from the coordinator: "
		"zdo bind on <our eui64> 10 <bulb eui64> <bulb ep> 0x0006",
		CONFIG_OCCUPANCY_OFF_DELAY_SEC);

	return 0;
}
