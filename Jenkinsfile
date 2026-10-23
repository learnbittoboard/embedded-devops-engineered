// Jenkinsfile — the finished factory (end of Chapter 10).
//
// Grown chapter by chapter, never replaced:
//   Ch 5  build + archive            Ch 8  sign + verify
//   Ch 6  gates + unit tests         Ch 9  tag -> approval -> release
//   Ch 7  version + matrix + manifest   Ch 10  hardware-in-the-loop
pipeline {
    agent none

    options {
        // One run at a time: a concurrent run lands in a
        // '<job>@2' workspace, and the '@' in that path breaks
        // Zephyr's devicetree dependency parsing (Ch 7) — and
        // two runs stamping versions at once is a race nobody
        // needs. Queue, don't race.
        disableConcurrentBuilds()
        // Disk is not an archive strategy (Chapter 7): keep the
        // newest 20 runs' artifacts; the manifest of everything
        // older is one JSON file per run and costs nothing to keep.
        buildDiscarder(logRotator(artifactNumToKeepStr: '20'))
    }

    environment {
        TOOLCHAIN_IMAGE = 'toolchain:1.0.0-ncs3.4.1'
        // Your Course 1 workspace on the Jenkins machine (Ch 5).
        NCS_WS = '/home/jenkins/ncs-course'
        BOARD = 'nrf52840dk/nrf52840'
    }

    stages {
        stage('Resolve') {
            // Runs on the Jenkins node itself (not in a container):
            // identify the toolchain image this run will use, by
            // digest when it came from a registry, by image ID when
            // it was built locally. The manifest records it — an
            // artifact that cannot name its build machine is not
            // provenance, it is a rumour.
            agent any
            steps {
                script {
                    env.TOOLCHAIN_REF = sh(
                        script: """docker image inspect \
                            -f '{{if .RepoDigests}}{{index .RepoDigests 0}}{{else}}${TOOLCHAIN_IMAGE}@{{.Id}}{{end}}' \
                            ${TOOLCHAIN_IMAGE}""",
                        returnStdout: true).trim()
                }
            }
        }

        stage('Version') {
            agent { docker { image "${TOOLCHAIN_IMAGE}"; args "-v ${NCS_WS}:/ws" } }
            steps {
                // The tag is the truth (Chapter 7): derive the
                // version, stamp it into app/VERSION, and keep it
                // for every later stage and the manifest.
                sh 'tools/stamp-version.sh > .version'
                script { env.PRODUCT_VERSION = readFile('.version').trim() }
            }
        }

        stage('Gates') {
            agent { docker { image "${TOOLCHAIN_IMAGE}"; args "-v ${NCS_WS}:/ws" } }
            steps {
                // Fastest failures first (Chapter 6). A gate that
                // cannot fail is a poster, not a gate — each of
                // these was armed by breaking it once in the lab.
                sh 'clang-format --dry-run --Werror app/src/*.c app/src/*.h app/tests/ld2410_parser/src/*.c'
                sh 'cppcheck --enable=warning,performance,portability --error-exitcode=1 --suppress=missingIncludeSystem --inline-suppr app/src/ld2410_parser.c'
            }
        }

        stage('Unit tests') {
            agent { docker { image "${TOOLCHAIN_IMAGE}"; args "-v ${NCS_WS}:/ws" } }
            steps {
                // Twister on the 64-bit native_sim (Chapter 6
                // explains the 32-bit multilib trap). The outdir
                // lives in a SUBDIRECTORY of the workspace mount —
                // Twister recycles its outdir and a mount root
                // cannot be recycled.
                sh '''
                    cd /ws
                    west twister -T "$WORKSPACE/app/tests" \
                        -p native_sim/native/64 \
                        --outdir "$WORKSPACE/twister-out"
                '''
            }
            post {
                always {
                    // Test results are build DATA, not log text:
                    // Jenkins graphs them, trends them, and points
                    // at the failing case by name.
                    junit 'twister-out/twister.xml'
                }
            }
        }

        // The matrix (Chapter 7): the product's two build legs in
        // parallel — the DK product and the dongle coordinator.
        // They share nothing but the workspace mount and the
        // toolchain image, which is exactly when parallel helps;
        // the chapter is honest about the wall-clock it does and
        // doesn't save on a two-core desk machine.
        stage('Build matrix') {
            parallel {
                stage('Build & sign') {
                    agent { docker { image "${TOOLCHAIN_IMAGE}"; args "-v ${NCS_WS}:/ws" } }
                    steps {
                // The signing key arrives as a bound FILE
                // credential (Chapter 8): Jenkins places it at a
                // private path, masks it in the log, and removes
                // it when the stage ends. The key itself is never
                // in this repo, this file, or any console log.
                withCredentials([file(credentialsId: 'mcuboot-product-key',
                                      variable: 'SIGNING_KEY')]) {
                    sh '''
                        cd /ws
                        west build -p always -b "$BOARD" \
                            -d "$WORKSPACE/build" --sysbuild "$WORKSPACE/app" \
                            -- -DZEPHYR_EXTRA_MODULES=/ws/ncs-zigbee \
                               -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$SIGNING_KEY\"
                    '''
                }
                // Trust, then verify — in that order, every run:
                // an artifact that fails signature verification
                // fails the build, before anything is archived.
                // (The PUBLIC key may live in the repo; it is the
                // private half that is the crown jewel.) imgtool
                // itself is MCUboot's own script from the workspace
                // — the same code that just signed the image.
                        sh 'python3 /ws/bootloader/mcuboot/scripts/imgtool.py verify -k keys/product-key.pub.pem build/app/zephyr/zephyr.signed.bin'
                        stash name: 'signed-image',
                              includes: 'build/app/zephyr/zephyr.signed.bin,build/app/zephyr/zephyr.signed.hex'
                    }
                }

                stage('Coordinator (dongle)') {
                    agent { docker { image "${TOOLCHAIN_IMAGE}"; args "-v ${NCS_WS}:/ws" } }
                    steps {
                        // The matrix's second leg (Chapter 7):
                        // Course 1's shell-sample coordinator for
                        // the dongle, USB variant — the other half
                        // of the product.
                        sh '''
                            cd /ws
                            west build -p always -b nrf52840dongle/nrf52840 \
                                -d "$WORKSPACE/build-coord" ncs-zigbee/samples/shell \
                                -- -DZEPHYR_EXTRA_MODULES=/ws/ncs-zigbee -DFILE_SUFFIX=usb
                        '''
                    }
                }
            }
        }

        stage('Manifest & archive') {
            agent { docker { image "${TOOLCHAIN_IMAGE}"; args "-v ${NCS_WS}:/ws" } }
            steps {
                sh '''
                    python3 tools/make-manifest.py \
                        --version "$PRODUCT_VERSION" \
                        --board "$BOARD" \
                        --toolchain-image "$TOOLCHAIN_REF" \
                        --out build/manifest.json \
                        build/app/zephyr/zephyr.signed.bin \
                        build/app/zephyr/zephyr.signed.hex \
                        build/app/zephyr/zephyr.elf \
                        build/dfu_application.zip \
                        build/*.zigbee \
                        build-coord/shell/zephyr/zephyr.hex
                '''
                archiveArtifacts artifacts: 'build/app/zephyr/zephyr.signed.bin,build/app/zephyr/zephyr.signed.hex,build/app/zephyr/zephyr.elf,build/dfu_application.zip,build/*.zigbee,build-coord/shell/zephyr/zephyr.hex,build/manifest.json',
                                 fingerprint: true
            }
        }

        stage('Hardware-in-the-loop') {
            // The pipeline's ground truth (Chapter 10): flash the
            // exact signed image this run built onto a real DK on
            // the desk rig, and interrogate it over serial. Gated:
            // the rig is one board — releases and nightly runs get
            // it, every commit does not.
            agent { label 'hil' }
            when {
                anyOf {
                    tag 'v*'
                    triggeredBy 'TimerTrigger'
                }
            }
            steps {
                unstash 'signed-image'
                // The flashing contract, stated in full: the
                // stage flashes the exact signed .hex THIS run
                // built (a .hex carries its load addresses; the
                // .bin stays the OTA/manifest artifact), at the
                // rig's serial port ($HIL_PORT, set on the hil
                // node), and expects the banner to report THIS
                // run's stamped version. --require-hardware turns
                // any missing piece into a FAILED stage — never
                // a green stage full of skips that tested nothing.
                sh '''
                    if [ -z "${HIL_PORT:-}" ]; then
                        echo "HIL_PORT is not set on this node: no rig, no pass" >&2
                        exit 1
                    fi
                    python3 -m pytest tests/hil \
                        --firmware build/app/zephyr/zephyr.signed.hex \
                        --port "$HIL_PORT" \
                        --expect-version "$PRODUCT_VERSION" \
                        --require-hardware \
                        --junitxml=hil-results.xml
                '''
            }
            post {
                always { junit 'hil-results.xml' }
            }
        }

        stage('Release') {
            agent any
            when { tag 'v*' }
            steps {
                // Automation proposes; a human disposes (Chapter 9).
                input message: "Publish release ${env.PRODUCT_VERSION}?",
                      ok: 'Publish'
                withCredentials([string(credentialsId: 'github-token',
                                        variable: 'GH_TOKEN')]) {
                    sh 'tools/make-release.sh build release'
                    sh 'tools/publish-release.sh release'
                }
            }
        }
    }
}
