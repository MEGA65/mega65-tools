pipeline {
    agent {
        docker { dockerfile true }
    }
    stages {
        stage('Build linux') {
            steps {
                sh 'make USE_LOCAL_CC65=0'
                sh 'make USE_LOCAL_CC65=0 test'
            }
        }
    }
}
