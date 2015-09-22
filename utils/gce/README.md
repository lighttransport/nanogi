
## Build nanogi worker image

    client $ bash build-image.sh

## Run master node

    client $ bash boost-master-instance.sh

### SSH portforwarding.

Use `redis-port-forward.sh` to connect to redis server in the master instance from the client.

    client $ bash redis-port-forward.sh

Then, open new terminal and you can connect redis with `localhost:6379`

    client $ redis-cli  # in the client
    127.0.0.1:6379>
    
