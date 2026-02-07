################################################################################
#
# ldd
#
################################################################################

LDD_VERSION = d38495cb5f318624dd15935e966c31eb63004369
LDD_SITE = git@github.com:vannidelprete/assignment7-vannidelprete.git
LDD_SITE_METHOD = git
LDD_MODULE_SUBDIRS = scull misc-modules

$(eval $(kernel-module))
$(eval $(generic-package))
