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
MAJOR_VERSION = 1
MINOR_VERSION = 8
VERSION_STRING = '%d.%02d' % [MAJOR_VERSION, MINOR_VERSION]

require 'sake1/component'

$comp = Sake::Component.new(:target_type => :pyd,
                            :basename => 'pyexpat',
                            :name => "xml library for PyS60",
                            :version => [1, 9],
                            :uid_v8 => COMP_UID,
                            :caps => [])

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
