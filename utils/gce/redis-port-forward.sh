#!/bin/bash

. config.sh

gcloud compute ssh ${GCE_MASTER_INSTANCE_NAME} --zone us-central1-a --ssh-flag="-L 6379:localhost:6379"
