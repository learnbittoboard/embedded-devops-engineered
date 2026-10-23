/*
 * Chapter 8, Step 2 — the Occupancy Sensor device declaration,
 * actuation edition.
 *
 * Chapter 7's endpoint, Chapter 8 Step 1's reporting context —
 * and one new line in the cluster list: an On/Off CLIENT cluster
 * (0x0006, client role, no attributes — a client holds nothing;
 * it *sends* the cluster's commands).
 *
 * The asymmetry is the Zigbee model in miniature. Occupancy
 * Sensing sits here as a SERVER: we hold the truth, others read
 * and subscribe. On/Off sits here as a CLIENT: the bulb holds
 * that truth; we send it commands (On, Off) — addressed, in this
 * design, to "whoever the binding table says", which is what
 * makes the coordinator's `zdo bind` the wiring step of the lab.
 *
 * The device remains, in the descriptor's eyes, an Occupancy
 * Sensor (0x0107): the client cluster appears in the simple
 * descriptor's OUT list (the flat cluster array below lists
 * server IDs first, then client IDs — the layout ZBOSS's own
 * device headers use).
 */

#ifndef ZB_OCCUPANCY_SENSOR_H
#define ZB_OCCUPANCY_SENSOR_H 1

/**
 *  @defgroup ZB_DEFINE_DEVICE_OCCUPANCY_SENSOR Occupancy Sensor
 *  @{
 *  @details
 *      - @ref ZB_ZCL_BASIC \n
 *      - @ref ZB_ZCL_IDENTIFY \n
 *      - @ref ZB_ZCL_OCCUPANCY_SENSING \n
 *      - @ref ZB_ZCL_ON_OFF (client)
 */

/** Occupancy Sensor Device ID (HA profile, ZCL device table) */
#define ZB_OCCUPANCY_SENSOR_DEVICE_ID 0x0107

/** Occupancy sensor device version */
#define ZB_DEVICE_VER_OCCUPANCY_SENSOR 0

/** @cond internals_doc */

/** Occupancy sensor IN (server) clusters number */
#define ZB_OCCUPANCY_SENSOR_IN_CLUSTER_NUM 3

/** Occupancy sensor OUT (client) clusters number */
#define ZB_OCCUPANCY_SENSOR_OUT_CLUSTER_NUM 1

#define ZB_OCCUPANCY_SENSOR_CLUSTER_NUM \
	(ZB_OCCUPANCY_SENSOR_IN_CLUSTER_NUM + ZB_OCCUPANCY_SENSOR_OUT_CLUSTER_NUM)

/** Number of attributes for reporting on Occupancy sensor device.
 * One: the Occupancy attribute is the cluster's only reportable
 * attribute (ZB_ZCL_OCCUPANCY_SENSING_REPORT_ATTR_COUNT).
 */
#define ZB_OCCUPANCY_SENSOR_REPORT_ATTR_COUNT \
	ZB_ZCL_OCCUPANCY_SENSING_REPORT_ATTR_COUNT

/** @endcond */ /* internals_doc */

/**
 * @brief Declare cluster list for Occupancy sensor device
 * @param cluster_list_name - cluster list variable name
 * @param basic_attr_list - attribute list for Basic cluster
 * @param identify_attr_list - attribute list for Identify cluster
 * @param occupancy_attr_list - attribute list for Occupancy Sensing cluster
 *
 * The On/Off client needs no attribute list parameter: a client
 * cluster instance is declared with 0 attributes and a NULL list
 * (the shape ZB_HA_DECLARE_ON_OFF_SWITCH_CLUSTER_LIST uses).
 */
#define ZB_DECLARE_OCCUPANCY_SENSOR_CLUSTER_LIST(			      \
		cluster_list_name,					      \
		basic_attr_list,					      \
		identify_attr_list,					      \
		occupancy_attr_list)					      \
zb_zcl_cluster_desc_t cluster_list_name[] =				      \
{									      \
	ZB_ZCL_CLUSTER_DESC(						      \
		ZB_ZCL_CLUSTER_ID_BASIC,				      \
		ZB_ZCL_ARRAY_SIZE(basic_attr_list, zb_zcl_attr_t),	      \
		(basic_attr_list),					      \
		ZB_ZCL_CLUSTER_SERVER_ROLE,				      \
		ZB_ZCL_MANUF_CODE_INVALID				      \
	),								      \
	ZB_ZCL_CLUSTER_DESC(						      \
		ZB_ZCL_CLUSTER_ID_IDENTIFY,				      \
		ZB_ZCL_ARRAY_SIZE(identify_attr_list, zb_zcl_attr_t),	      \
		(identify_attr_list),					      \
		ZB_ZCL_CLUSTER_SERVER_ROLE,				      \
		ZB_ZCL_MANUF_CODE_INVALID				      \
	),								      \
	ZB_ZCL_CLUSTER_DESC(						      \
		ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,			      \
		ZB_ZCL_ARRAY_SIZE(occupancy_attr_list, zb_zcl_attr_t),	      \
		(occupancy_attr_list),					      \
		ZB_ZCL_CLUSTER_SERVER_ROLE,				      \
		ZB_ZCL_MANUF_CODE_INVALID				      \
	),								      \
	ZB_ZCL_CLUSTER_DESC(						      \
		ZB_ZCL_CLUSTER_ID_ON_OFF,				      \
		0,							      \
		NULL,							      \
		ZB_ZCL_CLUSTER_CLIENT_ROLE,				      \
		ZB_ZCL_MANUF_CODE_INVALID				      \
	)								      \
}

/** @cond internals_doc */

/**
 * @brief Declare simple descriptor for Occupancy sensor device
 * @param ep_name - endpoint variable name
 * @param ep_id - endpoint ID
 * @param in_clust_num - number of supported input clusters
 * @param out_clust_num - number of supported output clusters
 */
#define ZB_ZCL_DECLARE_OCCUPANCY_SENSOR_SIMPLE_DESC(ep_name, ep_id, in_clust_num, out_clust_num) \
	ZB_DECLARE_SIMPLE_DESC(in_clust_num, out_clust_num);				       \
	ZB_AF_SIMPLE_DESC_TYPE(in_clust_num, out_clust_num) simple_desc_##ep_name =	       \
	{										       \
		ep_id,									       \
		ZB_AF_HA_PROFILE_ID,							       \
		ZB_OCCUPANCY_SENSOR_DEVICE_ID,						       \
		ZB_DEVICE_VER_OCCUPANCY_SENSOR,						       \
		0,									       \
		in_clust_num,								       \
		out_clust_num,								       \
		{									       \
			ZB_ZCL_CLUSTER_ID_BASIC,					       \
			ZB_ZCL_CLUSTER_ID_IDENTIFY,					       \
			ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,				       \
			ZB_ZCL_CLUSTER_ID_ON_OFF					       \
		}									       \
	}

/** @endcond */

/**
 * @brief Declare endpoint for Occupancy sensor device
 * @param ep_name - endpoint variable name
 * @param ep_id - endpoint ID
 * @param cluster_list - endpoint cluster list
 */
#define ZB_DECLARE_OCCUPANCY_SENSOR_EP(ep_name, ep_id, cluster_list)			      \
	ZB_ZCL_DECLARE_OCCUPANCY_SENSOR_SIMPLE_DESC(ep_name, ep_id,			      \
		ZB_OCCUPANCY_SENSOR_IN_CLUSTER_NUM, ZB_OCCUPANCY_SENSOR_OUT_CLUSTER_NUM);     \
	ZBOSS_DEVICE_DECLARE_REPORTING_CTX(reporting_info## ep_name,			      \
		ZB_OCCUPANCY_SENSOR_REPORT_ATTR_COUNT);				      \
	ZB_AF_DECLARE_ENDPOINT_DESC(ep_name, ep_id, ZB_AF_HA_PROFILE_ID, 0, NULL,	      \
		ZB_ZCL_ARRAY_SIZE(cluster_list, zb_zcl_cluster_desc_t), cluster_list,	      \
			(zb_af_simple_desc_1_1_t *)&simple_desc_##ep_name,		      \
			ZB_OCCUPANCY_SENSOR_REPORT_ATTR_COUNT,				      \
				reporting_info## ep_name,				      \
			0, NULL) /* No CVC ctx */

/*! @} */

#endif /* ZB_OCCUPANCY_SENSOR_H */
