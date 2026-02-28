
##############################################################
#
# AESD-ASSIGNMENTS
#
##############################################################

AESD_ASSIGNMENTS_VERSION = 4b38e5a6b54e401c01609ad3098c6f3009192031
AESD_ASSIGNMENTS_SITE = git@github.com:vannidelprete/assignment9-vannidelprete.git
AESD_ASSIGNMENTS_SITE_METHOD = git
AESD_ASSIGNMENTS_GIT_SUBMODULES = YES

define AESD_ASSIGNMENTS_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)/finder-app all
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)/server all
	$(MAKE) -C $(@D)/aesd-char-driver KERNELDIR=$(LINUX_DIR) CROSS_COMPILE=$(TARGET_CROSS) ARCH=$(KERNEL_ARCH) M=$(@D)/aesd-char-driver modules
endef

# Install writer, finder and finder-test utilities/scripts
define AESD_ASSIGNMENTS_INSTALL_TARGET_CMDS
	$(INSTALL) -d 0755 $(@D)/conf/ $(TARGET_DIR)/etc/finder-app/conf/
	$(INSTALL) -m 0755 $(@D)/conf/* $(TARGET_DIR)/etc/finder-app/conf/
	$(INSTALL) -m 0755 $(@D)/finder-app/writer $(TARGET_DIR)/usr/bin
	$(INSTALL) -m 0755 $(@D)/finder-app/finder.sh $(TARGET_DIR)/usr/bin
	$(INSTALL) -m 0755 $(@D)/finder-app/finder-test.sh $(TARGET_DIR)/usr/bin
	$(INSTALL) -m 0755 $(@D)/assignment-autotest/test/assignment4/* $(TARGET_DIR)/bin

	# Install aesdsocket application and startup script
	$(INSTALL) -m 0755 $(@D)/server/aesdsocket $(TARGET_DIR)/usr/bin
	$(INSTALL) -m 0755 $(@D)/server/aesdsocket-start-stop $(TARGET_DIR)/etc/init.d/S99aesdsocket
	$(INSTALL) -d 0755 $(TARGET_DIR)/lib/modules/$(BR2_LINUX_KERNEL_VERSION)/extra
	$(INSTALL) -m 0644 $(@D)/aesd-char-driver/aesdchar.ko $(TARGET_DIR)/lib/modules/$(BR2_LINUX_KERNEL_VERSION)/extra/aesdchar.ko
endef

$(eval $(generic-package))
