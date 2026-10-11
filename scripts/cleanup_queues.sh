#!/usr/bin/env bash
# Removes any leftover POSIX message queues created by mqtls::mq (named
# "/tls_msgQ_id_*" by default) or by the test suite ("/mqtls_test_*"), e.g.
# after a process was killed before it could unlink its own queue.
set -euo pipefail

MQUEUE_DIR="/dev/mqueue"

if [[ ! -d "${MQUEUE_DIR}" ]]; then
  echo "mqtls: ${MQUEUE_DIR} is not mounted; nothing to clean up." >&2
  exit 0
fi

removed=0
for entry in "${MQUEUE_DIR}"/tls_msgQ_id_* "${MQUEUE_DIR}"/mqtls_test_*; do
  [[ -e "${entry}" ]] || continue
  name="/$(basename "${entry}")"
  echo "Removing leftover queue: ${name}"
  rm -f "${entry}"
  removed=$((removed + 1))
done

if [[ "${removed}" -eq 0 ]]; then
  echo "mqtls: no leftover queues found."
else
  echo "mqtls: removed ${removed} leftover queue(s)."
fi
