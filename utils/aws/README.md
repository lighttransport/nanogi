# Run nanogi with AWS lambda

Replace cid with Docker container id in `extract_files.sh`, then

    $ ./extract_files.sh

Run `pkg.sh` to create zip package.


## Lambda

Upload .zip to S3.

set handler to `nanogi_lambda.js`
