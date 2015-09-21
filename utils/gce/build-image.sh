#!/bin/bash -x

PREFIX=nanogi
GCE_PROJECT=lighttransport.com:computeengine
GCE_ZONE=asia-east1-a
GCE_MACHINE_TYPE=n1-standard-8
GCE_IMAGE_PROJECT=ubuntu-os-cloud
GCE_IMAGE=ubuntu-1504-vivid-v20150911

gcloud compute images delete ${PREFIX}-image \
	  --quiet \
          --project ${GCE_PROJECT} \

set -e

gcloud compute instances create ${PREFIX}-builder \
	  --quiet \
          --project ${GCE_PROJECT} \
          --zone ${GCE_ZONE} \
          --machine-type ${GCE_MACHINE_TYPE} \
          --image-project ${GCE_IMAGE_PROJECT} \
          --image ${GCE_IMAGE} \
          --preemptible

# fixme.
sleep 30

gcloud compute copy-files setup-gce-image.sh ${PREFIX}-builder:~/ --zone ${GCE_ZONE} 

gcloud compute ssh --zone ${GCE_ZONE} nanogi-builder sudo sh setup-gce-image.sh

gcloud compute instances delete ${PREFIX}-builder \
	  --quiet \
          --keep-disks boot \
          --project ${GCE_PROJECT} \
          --zone ${GCE_ZONE}

gcloud compute images create ${PREFIX}-image \
	  --quiet \
          --source-disk ${PREFIX}-builder \
          --source-disk-zone ${GCE_ZONE} \
          --project ${GCE_PROJECT} \

gcloud compute disks delete ${PREFIX}-builder \
	  --quiet \
          --project ${GCE_PROJECT} \
          --zone ${GCE_ZONE}

