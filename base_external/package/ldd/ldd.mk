################################################################################
#
# ldd
#
################################################################################

LDD_VERSION = d38495cb5f318624dd15935e966c31eb63004369
LDD_SITE = git@github.com:vannidelprete/assignment7-vannidelprete.git
LDD_SITE_METHOD = git
LDD_MODULE_SUBDIRS = scull misc-modules

define LDD_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0755 $(@D)/scull/scull_load $(TARGET_DIR)/usr/bin
	$(INSTALL) -m 0755 $(@D)/scull/scull_unload $(TARGET_DIR)/usr/bin
	$(INSTALL) -m 0755 $(@D)/misc-modules/module_load $(TARGET_DIR)/usr/bin
	$(INSTALL) -m 0755 $(@D)/misc-modules/module_unload $(TARGET_DIR)/usr/bin
endef

$(eval $(kernel-module))
$(eval $(generic-package))
