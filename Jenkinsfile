// Jenkinsfile — the factory's first product (Chapter 5 state).
//
// The pipeline is CODE: it lives in the repo, is reviewed like
// firmware, and its history is the repo's history. Three honest
// stages: build the product inside the pinned toolchain image,
// archive what the build made. Chapter 6 adds the gates and tests,
// Chapter 7 the matrix and the version, Chapter 8 the signature —
// this file grows; it is never replaced.
pipeline {
    agent none

    environment {
        // The toolchain image from Chapters 2-3. Pinned by tag here;
        // the manifest (Chapter 7) records the digest it resolved to.
        TOOLCHAIN_IMAGE = 'toolchain:1.0.0-ncs3.4.1'
        // Your Course 1 workspace ON THE JENKINS MACHINE — the NCS
        // tree the build mounts at /ws. Set this to your path
        // (Chapter 5, step 2). The image deliberately does not
        // contain the workspace: code is mounted, tools are baked.
        NCS_WS = '/home/jenkins/ncs-course'
    }

    stages {
        stage('Build firmware') {
            agent {
                docker {
                    image "${TOOLCHAIN_IMAGE}"
                    // Mount the NCS workspace; the container's
                    // builder user owns what it writes into the
                    // Jenkins workspace, so artifacts are not
                    // root-owned on the controller.
                    args "-v ${NCS_WS}:/ws"
                }
            }
            steps {
                // west must run from inside the workspace (it finds
                // the tree by walking up from the cwd); the app and
                // the build dir live in the Jenkins workspace, which
                // the docker agent mounts at the same path.
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
                // Sysbuild layout: one directory per image under
                // the build root — app/zephyr/ for the product,
                // mcuboot/zephyr/ for the bootloader. The signed
                // image is the product; the ELF is its debug twin;
                // the DFU zip and the .zigbee package sit at the
                // build root.
                archiveArtifacts artifacts: 'build/app/zephyr/zephyr.signed.bin,build/app/zephyr/zephyr.signed.hex,build/app/zephyr/zephyr.elf,build/dfu_application.zip,build/*.zigbee',
                                 fingerprint: true
            }
        }
    }
}
