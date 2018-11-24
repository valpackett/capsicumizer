project('capsicumizer', ['cpp'], version: '0.0.1', license: 'Unlicense',
	default_options: ['cpp_std=c++14', 'warning_level=2'])

dep_libpreopen = dependency('libpreopen')
dep_libucl = dependency('libucl')

capsicumizer = executable('capsicumizer',
	'capsicumizer.cpp',
	cpp_args: ['-fno-rtti'],
	dependencies: [dep_libpreopen, dep_libucl],
	install: true)
