var exec = require('child_process').exec;

exports.js = function(event, context) {
    console.log("running nanogi...");
    child = exec('./nanogi', {env: { LD_LIBRARY_PATH: ".:$LD_LIBRARY_PATH"}}, function(error) {
        context.done(error, 'Process complete!');
    });

    // Log process stdout and stderr
    child.stdout.on('data', console.log);
    child.stderr.on('data', console.error);
};

