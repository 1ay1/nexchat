hexchat_text = executable('nexchat-text',
  sources: [
    'fe-text.c',
  ],
  dependencies: hexchat_common_dep,
  install: true,
)

test('smoke-text', find_program('sh'),
  args: [files('../../tests/smoke-text.sh'), hexchat_text],
  timeout: 60,
)

test('hostile-text', find_program('sh'),
  args: [files('../../tests/hostile-text.sh'), hexchat_text],
  timeout: 60,
)

test('smart-filter', find_program('sh'),
  args: [files('../../tests/smart-filter.sh'), hexchat_text],
  timeout: 60,
)

test('echo-message', find_program('sh'),
  args: [files('../../tests/echo-message.sh'), hexchat_text],
  timeout: 60,
)

test('batch', find_program('sh'),
  args: [files('../../tests/batch.sh'), hexchat_text],
  timeout: 60,
)

test('cmd-suggest', find_program('sh'),
  args: [files('../../tests/cmd-suggest.sh'), hexchat_text],
  timeout: 30,
)

test('chathistory', find_program('sh'),
  args: [files('../../tests/chathistory.sh'), hexchat_text],
  timeout: 60,
)

test('lastlog-logs', find_program('sh'),
  args: [files('../../tests/lastlog-logs.sh'), hexchat_text],
  timeout: 30,
)

test('reply', find_program('sh'),
  args: [files('../../tests/reply.sh'), hexchat_text],
  timeout: 60,
)

test('multiline', find_program('sh'),
  args: [files('../../tests/multiline.sh'), hexchat_text],
  timeout: 90,
)

if libssl_dep.found() and find_program('openssl', required: false).found()
  test('sts', find_program('python3'),
    args: [files('../../tests/sts.py'), hexchat_text],
    timeout: 60,
  )
endif

test('migrate-config', find_program('sh'),
  args: [files('../../tests/migrate-config.sh'), hexchat_text],
  timeout: 30,
)

# against real soju, ZNC and Ergo (tests/bnc/fetch.sh gets them; skipped without)
test('bouncers', find_program('sh'),
  args: [files('../../tests/bouncers.sh'), hexchat_text],
  timeout: 480,
)
test('bouncers-soju', find_program('sh'),
  args: [files('../../tests/bouncers-soju.sh'), hexchat_text],
  timeout: 480,
)
test('bouncers-search', find_program('sh'),
  args: [files('../../tests/bouncers-search.sh'), hexchat_text],
  timeout: 300,
)
test('redact', find_program('sh'),
  args: [files('../../tests/redact.sh'), hexchat_text],
  timeout: 120,
)
