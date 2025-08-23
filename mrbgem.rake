MRuby::Gem::Specification.new('mruby-poll') do |spec|
  spec.license = 'MIT'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'Low level system poll for mruby'
  spec.add_dependency 'mruby-errno'
  spec.add_dependency 'mruby-c-ext-helpers'
  spec.add_test_dependency 'mruby-io'

  if spec.for_windows?
    spec.linker.libraries << "ws2_32"
  end

  spec.cxx.defines << '_XOPEN_SOURCE'
end
