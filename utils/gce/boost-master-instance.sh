#!/bin/bash

. config.sh

gcloud compute instances create ${GCE_MASTER_INSTANCE_NAME} \
  --image container-vm \
  --metadata-from-file google-container-manifest=master-containers.yaml \
  --zone us-central1-a \
  --preemptible \
  --machine-type n1-standard-4
  
