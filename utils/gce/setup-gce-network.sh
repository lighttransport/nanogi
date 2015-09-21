#!/bin/bash

PREFIX=nanogi
GCE_PROJECT=lighttransport.com:computeengine
GCE_ZONE=asia-east1-a

gcloud compute networks create ${PREFIX}-cluster \
	  --range 10.240.0.0/16 \
          --project ${GCE_PROJECT}

# adding external firewall rule
# redis: 6379
gcloud compute firewall-rules create ${PREFIX}-external \
	  --quiet \
          --network ${PREFIX}-cluster \
          --source-ranges 0.0.0.0/0 \
          --allow tcp:22,tcp:6379 \
          --project ${GCE_PROJECT}

# adding internal firewall rule
gcloud compute firewall-rules create ${PREFIX}-internal \
          --network ${PREFIX}-cluster \
          --source-ranges 10.240.0.0/16 \
          --allow        tcp:1-65535,udp:1-65535,icmp \
          --quiet \
          --project ${GCE_PROJECT}

# create NAT route to external
gcloud compute routes create ${PREFIX}-internet-route \
          --network ${PREFIX}-cluster \
          --destination-range 0.0.0.0/0 \
          --next-hop-instance ${PREFIX}-master \
          --next-hop-instance-zone ${GCE_ZONE} \
          --tags ${PREFIX}-no-ip \
          --priority 800 \
          --quiet \
          --project ${GCE_PROJECT}
