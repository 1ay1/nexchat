hexchat_text = executable('hexchat-text',
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
