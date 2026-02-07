################################################################################
#
# ldd
#
################################################################################

LDD_VERSION = latest
LDD_SITE = git@github.com:vannidelprete/assignment7-vannidelprete.git
LDD_SITE_METHOD = git
LDD_MODULE_SUBDIRS = scull misc-modules

$(eval $(kernel-module))
$(eval $(generic-package))
