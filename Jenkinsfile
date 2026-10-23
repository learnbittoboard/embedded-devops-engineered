// Jenkinsfile — Chapter 6 state: gates and tests join the pipeline.
//
// Order is economics: the fastest failures run first. A format
// violation costs seconds, a unit test a minute, the full firmware
// build several — so the pipeline asks its questions cheapest-first,
// and a red build tells you at which stage to start reading.
pipeline {
    agent none

    environment {
        TOOLCHAIN_IMAGE = 'toolchain:1.0.0-ncs3.4.1'
        NCS_WS = '/home/jenkins/ncs-course'
    }

    stages {
        stage('Gates') {
            agent {
                docker {
                    image "${TOOLCHAIN_IMAGE}"
                    args "-v ${NCS_WS}:/ws"
                }
            }
            steps {
                // Format: the codebase took its one-time reformat
                // commit in Chapter 6's lab; from here the gate
                // holds the line. Static analysis: scoped to the
                // parser module — start where it is clean, expand
                // deliberately (the chapter explains why).
                sh 'clang-format --dry-run --Werror app/src/*.c app/src/*.h app/tests/ld2410_parser/src/*.c'
                sh 'cppcheck --enable=warning,performance,portability --error-exitcode=1 --suppress=missingIncludeSystem --inline-suppr app/src/ld2410_parser.c'
            }
        }

        stage('Unit tests') {
            agent {
                docker {
                    image "${TOOLCHAIN_IMAGE}"
                    args "-v ${NCS_WS}:/ws"
                }
            }
            steps {
                // The 64-bit native_sim — the 32-bit variant needs
                // a multilib host runtime and dies silently without
                // it (Chapter 6). The outdir is a SUBDIRECTORY of
                // the mounted workspace: Twister recycles its
                // outdir, and a mount root cannot be recycled.
                sh '''
                    cd /ws
                    west twister -T "$WORKSPACE/app/tests" \
                        -p native_sim/native/64 \
                        --outdir "$WORKSPACE/twister-out"
                '''
            }
            post {
                always {
                    junit 'twister-out/twister.xml'
                }
            }
        }

        stage('Build firmware') {
            agent {
                docker {
                    image "${TOOLCHAIN_IMAGE}"
                    args "-v ${NCS_WS}:/ws"
                }
            }
            steps {
                sh '''
                    cd /ws
                    west build -p always -b nrf52840dk/nrf52840 \
                        -d "$WORKSPACE/build" --sysbuild "$WORKSPACE/app" \
                        -- -DZEPHYR_EXTRA_MODULES=/ws/ncs-zigbee
                '''
            }
        }

        stage('Archive') {
            agent any
            steps {
                archiveArtifacts artifacts: 'build/app/zephyr/zephyr.signed.bin,build/app/zephyr/zephyr.signed.hex,build/app/zephyr/zephyr.elf,build/dfu_application.zip,build/*.zigbee',
                                 fingerprint: true
            }
        }
    }
}
