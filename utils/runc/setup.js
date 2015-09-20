var fs = require('fs')

if (process.argv.length < 6) {
  console.log("requires: input.config.json input.runtime.json uid gid")
  process.exit(-1)
}

var configFile = process.argv[2];
var runtimeFile = process.argv[3];
var uid = parseInt(process.argv[4]);
var gid = parseInt(process.argv[5]);
console.log(configFile);
console.log(runtimeFile);
console.log(uid);
console.log(gid);

// replace some value in config.json and runtime.json

var conf = JSON.parse(fs.readFileSync(configFile))
var runtime = JSON.parse(fs.readFileSync(runtimeFile))

conf.process.terminal = false
conf.process.user.uid = uid
conf.process.user.gid = gid
conf.root.readonly = false

fs.writeFileSync('config.json', JSON.stringify(conf, null, "  "))
fs.writeFileSync('runtime.json', JSON.stringify(runtime, null, "  "))
