i18n.gettext('nexchat', preset: 'glib')

validate_translations = find_program('validate-textevent-translations')
test('Validate translations', validate_translations,
  args: [
    join_paths(meson.current_source_dir(), 'LINGUAS'),
    meson.current_source_dir(),
  ]
)
