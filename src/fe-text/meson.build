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
