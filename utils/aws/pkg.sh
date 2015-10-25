#!/bin/bash

cp nanogi_lambda.js dist/
(cd dist; zip nanogi.zip *)
mv dist/nanogi.zip .
