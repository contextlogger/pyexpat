# -*- ruby -*-
# sake variant sake1

# The build tool driving this makefile is a custom one. To get it, you may use the command
#
#   svn co https://pdis-miso.svn.sourceforge.net/svnroot/pdis-miso/trunk/sake

# See also makefile.rb, which has useful utilities for reconstructing
# the source code, for instance.

# We could otherwise simply use a 0 value
# (as Python imports should be purely name-based),
# but SIS file prerequisites do require a UID,
# so we are using a UID allocated from Symbian.
COMP_NAME = "pyexpat"
COMP_UID = 0x10206ba2 # allocated from Symbian

require 'sake1/component'

$comp = Sake::Component.new(:target_type => :pyd,
                            :vendor => "HIIT",
                            :basename => 'pyexpat',
                            :name => "xml library for PyS60",
                            :version => [1, 9],
                            :uid_v8 => COMP_UID,
                            :caps => Sake::ALL_CAPS)

class Sake::Component
  def group_dir
    @dir
  end
end

require 'build/with'

def try_load file
  begin
    load file
  rescue LoadError; end
end

if $sake_op[:kits]
  $kits = Sake::DevKits::get_exact_set($sake_op[:kits].strip.split(/,/))
else
  $kits = Sake::DevKits::get_all
  $kits.delete_if do |kit|
    !kit.supports_python?
  end
end

$builds = $kits.map do |kit|
  Sake::CompBuild.new :component => $comp, :devkit => kit
end

# For any v9 builds, configure certificate info for signing. See
# Sake::CompBuild for the attributes to set.
try_load('local/signing.rb')

$builds.delete_if do |build|
  build.sign and !build.cert_file
end

if $sake_op[:builds]
  blist = $sake_op[:builds]
  $builds.delete_if do |build|
    !blist.include?(build.handle)
  end
end

class HexNum
  def initialize num
    @num = num
  end

  def to_s
    "0x%08x" % @num
  end
end

for build in $builds
  # To define __UID__ for header files.
  build.trait_map[:uid] = HexNum.new(build.uid3.number)
end

for build in $builds
  map = build.trait_map
  if $sake_op[:logging] and map[:has_flogger]
    map[:do_logging] = :define
  end

  build.plats = (build.v9? ? "gcce" : "armi")
end

task :default => [:bin, :sis]

require 'sake1/tasks'

Sake::Tasks::def_list_devices_tasks(:builds => $builds)

Sake::Tasks::def_makefile_tasks(:builds => $builds)

Sake::Tasks::def_binary_tasks(:builds => $builds)

Sake::Tasks::def_sis_tasks(:builds => $builds)

Sake::Tasks::def_clean_tasks(:builds => $builds)

task :all => [:makefiles, :bin, :sis]

Sake::Tasks::force_uncurrent_on_op_change

class Array
  def hjoin
    self.join('-')
  end
end

def sh_noerr(command)
  puts command
  system(command)
end

def make_patch(dir, basename, version)
  sfile = [basename, version, 'orig'].hjoin
  tfile = [basename, version, 'patched'].hjoin
  pfile = File.join(dir, ['patch', basename, version].hjoin)
  raise unless File.directory?(sfile)
  raise unless File.directory?(tfile)
  sh_noerr("diff -r -u -N --strip-trailing-cr #{sfile} #{tfile} > #{pfile}")
end

task :patches do
  patch_home = "patchfiles"
  mkdir_p patch_home
  list = Dir['*-*-patched']
  list.each do |pdir|
    if pdir =~ /(.*)-(.*)-patched/
      make_patch(patch_home, $1, $2)
    else
      raise
    end
  end  
end

task :web do
  srcfiles = Dir['web/*.txt2tags.txt']
  for srcfile in srcfiles
    htmlfile = srcfile.sub(/\.txt2tags\.txt$/, ".html")
    sh("tools/txt2tags --target xhtml --infile %s --outfile %s --encoding utf-8 --verbose" % [srcfile, htmlfile])
  end
end

task :upload do
  ruby("local/upload.rb")
end

task :upload_dry do
  ruby("local/upload.rb", "dry")
end

def sis_info opt
  for build in $builds
    if build.short_sisx_file.exist?
      sh("sisinfo -f #{build.short_sisx_file} #{opt}")
    end
  end
end

task :sis_ls do
  sis_info "-i"
end

task :sis_cert do
  sis_info "-c"
end

task :sis_struct do
  sis_info "-s"
end
