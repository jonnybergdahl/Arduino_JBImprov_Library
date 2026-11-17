#!/usr/bin/env zsh
# Compile every example for every board it supports, using the environments in
# platformio.ini. Usage: scripts/build_examples.sh [env ...]
set -e
cd "${0:A:h}/.."

envs=("$@")
if (( ${#envs} == 0 )); then
  envs=(esp32dev esp32-s3 esp32-s2 esp32dev-core2 d1_mini)
fi

# BLE examples need an ESP32 with Bluetooth.
ble_envs=(esp32dev esp32-s3 esp32dev-core2)

failed=()
for env in $envs; do
  for example in examples/*/; do
    name=${example:t}
    if [[ $name != SerialProvisioning ]] && (( ! ${ble_envs[(Ie)$env]} )); then
      continue
    fi
    echo "=== $name on $env"
    if ! pio ci "$example$name.ino" --lib . --project-conf platformio.ini -e "$env"; then
      failed+=("$name on $env")
    fi
  done
done

if (( ${#failed} )); then
  echo "Failed:"
  printf '  %s\n' $failed
  exit 1
fi
echo "All examples built."
