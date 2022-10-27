pipeline {
    agent {
        docker { image 'ubuntu:jammy' }
    }
    stages {
        stage('Setup') {
            steps {
                sh 'sudo apt-get install -y libusb-1.0-0-dev libgif-dev libpng-dev mingw-w64-common libz-mingw-w64-dev libgtest-dev libgmock-dev libpng-mingw-w64 libusb-1.0-0-mingw-w64 cc65'
            }
        }
    }
}
