#!/bin/bash

gcloud compute instances create test-vm \
  --image container-vm \
  --metadata-from-file google-container-manifest=master-containers.yaml \
  --zone us-central1-a \
  --preemptible \
  --machine-type f1-micro
  
