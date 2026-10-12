const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const { spawnSync } = require("node:child_process");

const root = path.resolve(__dirname, "..");
const cmake = process.env.CMAKE_EXE || "cmake";
const script = path.join(root, "cmake", "publish_firmware.cmake");

test("publication uses real binary metadata and immutable versioned releases", () => {
  const dir = path.join(__dirname, `.publication-${process.pid}`);
  fs.mkdirSync(dir);
  try {
    const source = path.join(dir, "source.bin");
    const destination = path.join(dir, "nas");
    fs.mkdirSync(destination);
    const bytes = Buffer.from("test ESP image bytes, not ELF descriptor");
    fs.writeFileSync(source, bytes);
    const publish = (version, urlPath = "") => spawnSync(cmake, [
      `-DSOURCE_FILE=${source}`, `-DDESTINATION_DIR=${destination}${path.sep}`,
      `-DFIRMWARE_VERSION=${version}`, "-DFIRMWARE_PROJECT=ota_suite_demo",
      "-DFIRMWARE_TARGET=esp32s3", "-DFIRMWARE_ORIGIN=https://updates.example.org",
      `-DFIRMWARE_URL_PATH=${urlPath}`,
      "-P", script,
    ], { encoding: "utf8" });
    let result = publish("1.2.0");
    assert.equal(result.status, 0, result.error?.message || result.stderr);
    const manifestFile = path.join(destination, "firmware.json");
    const manifest = JSON.parse(fs.readFileSync(manifestFile));
    assert.equal(manifest.version, "1.2.0");
    assert.equal(manifest.size, bytes.length);
    assert.equal(manifest.sha256, crypto.createHash("sha256").update(bytes).digest("hex"));
    assert.equal(manifest.target, "esp32s3");
    assert.equal(manifest.url, "https://updates.example.org/ota_suite_demo-1.2.0.bin");
    assert.deepEqual(fs.readFileSync(path.join(destination, "ota_suite_demo-1.2.0.bin")), bytes);
    assert.deepEqual(fs.readFileSync(path.join(destination, "ota_suite_demo.bin")), bytes);
    assert.ok(!fs.readdirSync(destination).some(name => name.endsWith(".uploading")));
    result = publish("1.2.0");
    assert.equal(result.status, 0, result.stderr);
    result = publish("1.2.0", "/BatteryTester");
    assert.equal(result.status, 0, result.stderr);
    assert.equal(JSON.parse(fs.readFileSync(manifestFile)).url,
      "https://updates.example.org/BatteryTester/ota_suite_demo-1.2.0.bin");
    const before = fs.readFileSync(manifestFile);
    for (const invalidPath of ["/../private", "/BatteryTester/", "BatteryTester", "/bad?query"]) {
      result = publish("1.2.0", invalidPath);
      assert.notEqual(result.status, 0);
      assert.match(result.stderr, /Invalid FIRMWARE_URL_PATH/);
      assert.deepEqual(fs.readFileSync(manifestFile), before);
    }
    fs.writeFileSync(source, "different bytes");
    result = publish("1.2.0");
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /Immutable release/);
    assert.deepEqual(fs.readFileSync(manifestFile), before);
    assert.deepEqual(fs.readFileSync(path.join(destination, "ota_suite_demo.bin")), bytes);
    result = publish("1.2.1");
    assert.equal(result.status, 0, result.stderr);
    assert.equal(JSON.parse(fs.readFileSync(manifestFile)).version, "1.2.1");
    assert.deepEqual(fs.readFileSync(path.join(destination, "ota_suite_demo-1.2.0.bin")), bytes);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test("OTA checks downloaded binary integrity before finalization and boot selection", () => {
  const source = fs.readFileSync(path.join(root, "components", "ota_manager", "ota_manager.c"), "utf8");
  const perform = source.slice(source.indexOf("static void perform_update"), source.indexOf("static void ota_worker"));
  assert.ok(perform.indexOf("psa_hash_update") < perform.indexOf("esp_ota_end(handle)"));
  assert.ok(perform.indexOf("received != selected.size") < perform.indexOf("esp_ota_end(handle)"));
  assert.ok(perform.indexOf("memcmp(digest, selected.sha256") < perform.indexOf("esp_ota_end(handle)"));
  assert.ok(perform.indexOf("esp_ota_end(handle)") < perform.indexOf("esp_ota_set_boot_partition(next)"));
  assert.match(perform, /esp_ota_abort\(handle\)/);
  assert.match(perform, /\.disable_auto_redirect = true/);
  assert.doesNotMatch(perform, /esp_https_ota_finish/);
  const checker = fs.readFileSync(path.join(root, "components", "update_check", "update_check.c"), "utf8");
  assert.doesNotMatch(checker, /esp_ota_begin|esp_ota_write|esp_ota_set_boot_partition|ota_manager_start/);
  assert.match(checker, /CHECK_INTERVAL_US \(6LL \* 60 \* 60 \* 1000000\)/);
  assert.match(checker, /RETRY_GUARD_US \(60LL \* 1000000\)/);
});
