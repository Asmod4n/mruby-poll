MRuby::Gem::Specification.new('mruby-poll') do |spec|
  spec.license = 'MIT'
  spec.author  = 'Hendrik Beskow'
  spec.summary = 'Low level system poll for mruby'
  spec.add_dependency 'mruby-errno'

  spec.cc.defines << '_XOPEN_SOURCE'
end
