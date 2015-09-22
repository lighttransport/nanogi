#!/bin/bash

. config.sh

# Delete exising insntance(if exists)
gcloud compute instances delete ${GCE_MASTER_INSTANCE_NAME} \
  --quiet \
  --zone ${GCE_ZONE} \

# Use startup script to explicitly shutdown the instance after 60 min
gcloud compute instances create ${GCE_MASTER_INSTANCE_NAME} \
  --quiet \
  --image container-vm \
  --metadata-from-file google-container-manifest=master-containers.yaml \
  --zone ${GCE_ZONE} \
  --preemptible \
  --machine-type ${GCE_MASTER_MACHINE_TYPE} \
  --metadata startup-script="nohup sudo /sbin/shutdown -h -P +60 &"
