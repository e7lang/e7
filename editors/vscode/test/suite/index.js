const path = require("path");
const Mocha = require("mocha");

exports.run = function run() {
  const mocha = new Mocha({ ui: "bdd", color: false, timeout: 60000 });
  mocha.addFile(path.resolve(__dirname, "extension.test.js"));
  return new Promise((resolve, reject) => {
    mocha.run((failures) => (failures ? reject(new Error(`${failures} test(s) failed`)) : resolve()));
  });
};
