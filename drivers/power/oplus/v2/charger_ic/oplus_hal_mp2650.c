// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[MP2650]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
#include <mt-plat/mtk_gpio.h>
#include <linux/xlog.h>
#include <mt-plat/mtk_rtc.h>
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0))
#include <mt-plat/charging.h>
#endif
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <soc/oplus/device_info.h>
extern void mt_power_off(void);
#else /* CONFIG_OPLUS_CHARGER_MTK */
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/rtc.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/boot_mode.h>
#endif

#include "oplus_hal_mp2650.h"
#include <oplus_chg_vooc.h>
#include <oplus_mms.h>
#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>
#include <oplus_mms_wired.h>
#include <oplus_battery_log.h>

/* TODO */
#define WPC_TERMINATION_CURRENT		100
#define WPC_TERMINATION_VOLTAGE		4500
#define WPC_RECHARGE_VOLTAGE_OFFSET	200
#define WPC_CHARGE_CURRENT_DEFAULT	500
#define WPC_PRECHARGE_CURRENT		480

#define MP2762_VSYS_THR_104		0x2
#define MP2762_VSYS_THR_101		0x3

#define DEBUG_BY_FILE_OPS

#define HW_AICL_POINT_HIGH		4520
#define HW_AICL_POINT_LOW		4440
#define SW_AICL_POINT_HIGH		4535
#define SW_AICL_POINT_LOW		4500
#define SWITCH_AICL_POINT_VBAT_HIGH	4140
#define SWITCH_AICL_POINT_VBAT_LOW	4000
#define AICL_POINT_SWITCH_THRE		7500
#define AICL_POINT_VOL_9V		8500

struct chip_mp2650 *charger_ic_2 = NULL;
int reg_access_allow_2 = 0;
int mp2650_2_reg_2 = 0;
static int aicl_result = 500;
void mp2650_2_wireless_set_mps_otg_en_val_2(int value);
int mp2650_2_get_vbus_voltage_2(void);
static int mp2650_2_set_charger_vsys_threshold(struct chip_mp2650 *chip, int val);
static int mp2650_2_burst_mode_enable(bool enable);
static int mp2650_2_reg_2_dump_internal(void);

#define BATTERY_LOG_REG_MAX_SIZE 100
char buck_ic_reg_info_2[BATTERY_LOG_REG_MAX_SIZE] = {0};

static DEFINE_MUTEX(mp2650_2_i2c_access);

#define RESUME_TIMEDOUT_MS	1000
static int mp2650_2_wait_resume(struct chip_mp2650 *chip)
{
	int rc;

	rc = wait_for_completion_timeout(&chip->resume_ack, msecs_to_jiffies(RESUME_TIMEDOUT_MS));
	if (!rc) {
		chg_err("wait resume timedout\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static int __tongfeng_test_mp2650_2_write_reg(int reg, int val)
{
	int ret = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0) {
		chg_err("i2c write fail: can't write %02x to %02x: %d\n", val,
			reg, ret);
		return ret;
	}

	return 0;
}

static ssize_t mp2650_2_reg_2_access_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
#ifdef OPLUS_CHG_UNDEF
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
#endif

	return snprintf(buf, PAGE_SIZE, "0x%02x\n", reg_access_allow_2);
}
static ssize_t mp2650_2_reg_2_access_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
#ifdef OPLUS_CHG_UNDEF
#ifdef TIMED_OUTPUT
	struct timed_output_dev *to_dev = dev_get_drvdata(dev);
	struct aw8697 *aw8697 = container_of(to_dev, struct aw8697, to_dev);
#else
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct aw8697 *aw8697 = container_of(cdev, struct aw8697, cdev);
#endif
#endif
	unsigned int val = 0;
	int rc = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	chg_err("%s: value=%d\n", __FUNCTION__, val);
	reg_access_allow_2 = val;

	return count;
}
static int mp2650_2_read_reg(int reg, int *returnData);
static int mp2650_2_read_reg(int reg, int *returnData);

static ssize_t mp2650_2_reg_2_set_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	int reg_val = 0;

	mp2650_2_read_reg(mp2650_2_reg_2, &reg_val);
	count += snprintf(buf + count, PAGE_SIZE - count,
			  "reg[0x%02x]: 0x%02x\n", mp2650_2_reg_2, reg_val);
	return count;
}

static ssize_t mp2650_2_reg_2_set_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	unsigned int databuf[2] = { 0, 0 };

	if (2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
		chg_err("%s: reg[0x%02x]=0x%02x\n", __FUNCTION__, databuf[0],
		       databuf[1]);
		mp2650_2_reg_2 = databuf[0];
		__tongfeng_test_mp2650_2_write_reg((int)databuf[0],
						 (int)databuf[1]);
	}
	return count;
}

static ssize_t mp2650_2_reg_2s_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int i = 0;
	int reg_val = 0;

	for (i = mp2650_2_FIRST_REG; i <= 0x48; i++) {
		mp2650_2_read_reg(i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02x=0x%02x \n", i, reg_val);
	}

	return len;
}
static ssize_t mp2650_2_reg_2s_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	return count;
}

static DEVICE_ATTR(reg_access, S_IWUSR | S_IRUGO, mp2650_2_reg_2_access_show,
		   mp2650_2_reg_2_access_store);
static DEVICE_ATTR(reg_set, S_IWUSR | S_IRUGO, mp2650_2_reg_2_set_show,
		   mp2650_2_reg_2_set_store);
static DEVICE_ATTR(read_regs, S_IWUSR | S_IRUGO, mp2650_2_reg_2s_show,
		   mp2650_2_reg_2s_store);

static struct attribute *mp2650_2_attributes[] = { &dev_attr_reg_access.attr,
						 &dev_attr_reg_set.attr,
						 &dev_attr_read_regs.attr,
						 NULL };

static struct attribute_group mp2650_2_attribute_group = {
	.attrs = mp2650_2_attributes
};

static int __mp2650_2_read_reg(int reg, int *returnData)
{
	int ret = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	ret = i2c_smbus_read_byte_data(chip->client, (unsigned char)reg);
	if (ret < 0) {
		chg_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
		return ret;
	} else {
		*returnData = ret;
	}

	return 0;
}

static int mp2650_2_read_reg(int reg, int *returnData)
{
	int ret = 0;
	/*    struct chip_mp2650 *chip = charger_ic_2; */

	mutex_lock(&mp2650_2_i2c_access);
	ret = __mp2650_2_read_reg(reg, returnData);
	mutex_unlock(&mp2650_2_i2c_access);
	return ret;
}

static int __mp2650_2_write_reg(int reg, int val)
{
	int ret = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (reg_access_allow_2 != 0) {
		return 0;
	}
	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0) {
		chg_err("i2c write fail: can't write %02x to %02x: %d\n", val,
			reg, ret);
		return ret;
	}

	return 0;
}

/**********************************************************
  *
  *   [Read / Write Function]
  *
  *********************************************************/
/*
*/

static int mp2650_2_config_interface(int RegNum, int val, int MASK)
{
	int mp2650_2_reg_2 = 0;
	int ret = -1;
	int count = 0;

	mutex_lock(&mp2650_2_i2c_access);
	while (count++ < 3 && ret != 0) {
		ret = __mp2650_2_read_reg(RegNum, &mp2650_2_reg_2);

		if (ret != 0)
			msleep(10);
	}
	if (ret != 0) {
		mutex_unlock(&mp2650_2_i2c_access);
		return ret;
	}
	/* chg_err(" Reg[%x]=0x%x %d\n", RegNum, mp2650_2_reg_2, MASK); */

	mp2650_2_reg_2 &= ~MASK;
	mp2650_2_reg_2 |= val;

	/* chg_err(" write Reg[%x]=0x%x %d\n", RegNum, mp2650_2_reg_2, val); */

	count = 0;
	ret = -1;
	while (count++ < 3 && ret != 0) {
		ret = __mp2650_2_write_reg(RegNum, mp2650_2_reg_2);

		if (ret != 0)
			msleep(10);
	}
	/* ret = __mp2650_2_read_reg(RegNum, &mp2650_2_reg_2); */

	/* chg_err(" Check Reg[%x]=0x%x\n", RegNum, mp2650_2_reg_2); */

	mutex_unlock(&mp2650_2_i2c_access);

	return ret;
}

/*this interface just for busrt mode use, do not add any other mutex */
static int mp2650_2_config_interface_without_lock(int RegNum, int val, int MASK)
{
	int mp2650_2_reg_2 = 0;
	int ret = -1;
	int count = 0;

	while (count++ < 3 && ret != 0) {
		ret = __mp2650_2_read_reg(RegNum, &mp2650_2_reg_2);

		if (ret != 0)
			msleep(10);
	}
	if (ret != 0) {
		return ret;
	}

	mp2650_2_reg_2 &= ~MASK;
	mp2650_2_reg_2 |= val;

	count = 0;
	ret = -1;
	while (count++ < 3 && ret != 0) {
		ret = __mp2650_2_write_reg(RegNum, mp2650_2_reg_2);

		if (ret != 0)
			msleep(10);
	}

	return ret;
}

int mp2650_2_mms_set_vindpm_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv) /* default 4.5V */
{
	int rc;
	int tmp = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	tmp = (vol_mv - REG01_mp2650_2_VINDPM_THRESHOLD_OFFSET) /
	      REG01_mp2650_2_VINDPM_THRESHOLD_STEP;
	rc = mp2650_2_config_interface(REG01_mp2650_2_ADDRESS,
				     tmp << REG01_mp2650_2_VINDPM_THRESHOLD_SHIFT,
				     REG01_mp2650_2_VINDPM_THRESHOLD_MASK);

	return rc;
}

int mp2650_2_set_vindpm_vol_2(int vol) /* default 4.5V */
{
	int rc;
	int tmp = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	tmp = (vol - REG01_mp2650_2_VINDPM_THRESHOLD_OFFSET) /
	      REG01_mp2650_2_VINDPM_THRESHOLD_STEP;
	rc = mp2650_2_config_interface(REG01_mp2650_2_ADDRESS,
				     tmp << REG01_mp2650_2_VINDPM_THRESHOLD_SHIFT,
				     REG01_mp2650_2_VINDPM_THRESHOLD_MASK);

	return rc;
}

int mp2650_2_usbin_input_current_limit_2[] = {
	500, 800, 1000, 1200, 1500, 1750, 2000, 2200, 2500, 2750, 3000,
};

int mp2650_2_input_Ilimit_disable_2(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	return rc;
}

int mp2650_2_input_current_limit_ctrl_by_vooc_write_2(int current_ma)
{
	int rc = 0;
	int tmp = 0;
	int count = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_read_reg(REG00_mp2650_2_ADDRESS, &tmp);
	if (rc) {
		chg_err("Couldn't read REG00_mp2650_2_ADDRESS rc = %d\n", rc);
		return 0;
	}

	mp2650_2_set_charger_vsys_threshold(chip, MP2762_VSYS_THR_104);
	mp2650_2_burst_mode_enable(false);

	tmp = tmp * 50;
	for (count = (tmp / 500); count > 2; count--) {
		chg_err("set charge current limit = %d\n", 500 * count);
		tmp = 500 * count - REG00_mp2650_2_1ST_CURRENT_LIMIT_OFFSET;
		tmp = tmp / REG00_mp2650_2_1ST_CURRENT_LIMIT_STEP;
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			tmp << REG00_mp2650_2_1ST_CURRENT_LIMIT_SHIFT,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			tmp << REG0F_mp2650_2_2ND_CURRENT_LIMIT_SHIFT,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		msleep(25);
	}

	for (count = 3; count <= (current_ma / 500); count++) {
		chg_err("set charge current limit = %d\n", 500 * count);
		tmp = 500 * count - REG00_mp2650_2_1ST_CURRENT_LIMIT_OFFSET;
		tmp = tmp / REG00_mp2650_2_1ST_CURRENT_LIMIT_STEP;
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			tmp << REG00_mp2650_2_1ST_CURRENT_LIMIT_SHIFT,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			tmp << REG0F_mp2650_2_2ND_CURRENT_LIMIT_SHIFT,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		msleep(25);
	}
	chg_err("set charge ctrl_by_vooc current limit = %d\n", current_ma);
	tmp = current_ma - REG00_mp2650_2_1ST_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG00_mp2650_2_1ST_CURRENT_LIMIT_STEP;

	rc = mp2650_2_config_interface(
		REG00_mp2650_2_ADDRESS,
		tmp << REG00_mp2650_2_1ST_CURRENT_LIMIT_SHIFT,
		REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(
		REG0F_mp2650_2_ADDRESS,
		tmp << REG0F_mp2650_2_2ND_CURRENT_LIMIT_SHIFT,
		REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);

	mp2650_2_burst_mode_enable(true);
	mp2650_2_set_charger_vsys_threshold(chip, MP2762_VSYS_THR_101);

	return rc;
}

static int mp2650_2_get_charger_vol(struct chip_mp2650 *chip)
{
	struct oplus_chg_ic_dev *parent = chip->ic_dev->parent;
	int vbus;
	int rc;

	if (!parent) {
		chg_err("parent ic not found\n");
		return 0;
	}

	rc = oplus_chg_ic_func(parent, OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL, &vbus);
	if (rc < 0) {
		chg_err("can't get vbus, rc=%d\n", rc);
		return 0;
	}
	return vbus;
}

int mp2650_2_get_pre_icl_index_2(void)
{
	int reg_val = 0;
	int icl_index = 0;
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = mp2650_2_read_reg(REG00_mp2650_2_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG00_mp2650_2_ADDRESS rc = %d\n", rc);
		return icl_index;
	}

	reg_val &= REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK;

	switch (reg_val) {
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_500MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_0;
		break;
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_800MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_1;
		break;
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_1000MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_2;
		break;
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_1200MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_3;
		break;
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_1500MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_4;
		break;
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_1750MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_5;
		break;
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_2000MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_6;
		break;
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_2200MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_7;
		break;
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_2500MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_8;
		break;
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_2750MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_9;
		break;
	case REG00_mp2650_2_1ST_CURRENT_LIMIT_3000MA:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_10;
		break;
	default:
		icl_index = INPUT_CURRENT_LIMIT_INDEX_0;
		break;
	}

	chg_debug("pre icl=%d setting %02x\n",
		  mp2650_2_usbin_input_current_limit_2[icl_index], icl_index);
	return icl_index;
}

static int oplus_vooc_get_fastchg_started(void)
{
	int fastchg_started_status = 0;
	struct oplus_mms *vooc_topic;
	union mms_msg_data data = { 0 };
	int rc;

	vooc_topic = oplus_mms_get_by_name("vooc");
	if (!vooc_topic)
		return 0;

	rc = oplus_mms_get_item_data(vooc_topic, VOOC_ITEM_VOOC_STARTED, &data, true);
	if (!rc)
		fastchg_started_status = data.intval;

	chg_info("get fastchg started status = %d\n", fastchg_started_status);
	return fastchg_started_status;
}

static int mp2650_2_input_current_limit_write(struct chip_mp2650 *chip,
					    int current_ma)
{
	int i = 0, rc = 0;
	int chg_vol = 0;
	int chg_type = 0;
	int pre_icl_index = 0;
	int sw_aicl_point = 0;
	int hw_aicl_point = 0;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	for (i = ARRAY_SIZE(mp2650_2_usbin_input_current_limit_2) - 1; i >= 0;
	     i--) {
		if (mp2650_2_usbin_input_current_limit_2[i] <= current_ma) {
			break;
		} else if (i == 0) {
			break;
		}
	}

	chg_info("usb input max current limit=%d setting %02x\n", current_ma,
		  i);

	pre_icl_index = mp2650_2_get_pre_icl_index_2();
	if (pre_icl_index < 2) {
		/* <1.2A, have been set 500 in bqmp2650_2_hardware_init_2 */
	} else {
		for (i = pre_icl_index - 1; i > 0; i--) {
			switch (i) {
			case INPUT_CURRENT_LIMIT_INDEX_1:
				rc = mp2650_2_config_interface(
					REG00_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_800MA,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(
					REG0F_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_800MA,
					REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_2:
				rc = mp2650_2_config_interface(
					REG00_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_1000MA,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(
					REG0F_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_1000MA,
					REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_3:
				rc = mp2650_2_config_interface(
					REG00_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_1200MA,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(
					REG0F_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_1200MA,
					REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_4:
				rc = mp2650_2_config_interface(
					REG00_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_1500MA,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(
					REG0F_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_1500MA,
					REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_5:
				rc = mp2650_2_config_interface(
					REG00_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_1750MA,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(
					REG0F_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_1750MA,
					REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_6:
				rc = mp2650_2_config_interface(
					REG00_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_2000MA,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(
					REG0F_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_2000MA,
					REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_7:
				rc = mp2650_2_config_interface(
					REG00_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_2200MA,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(
					REG0F_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_2200MA,
					REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_8:
				rc = mp2650_2_config_interface(
					REG00_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_2500MA,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(
					REG0F_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_2500MA,
					REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_9:
				rc = mp2650_2_config_interface(
					REG00_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_2750MA,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(
					REG0F_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_2750MA,
					REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case INPUT_CURRENT_LIMIT_INDEX_10:
				rc = mp2650_2_config_interface(
					REG00_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_3000MA,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(
					REG0F_mp2650_2_ADDRESS,
					REG00_mp2650_2_1ST_CURRENT_LIMIT_3000MA,
					REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			default:
				break;
			}
			msleep(50);
		}
	}

	chg_vol = mp2650_2_get_charger_vol(chip);
	chg_type = oplus_wired_get_chg_type();
	if (chg_vol > AICL_POINT_SWITCH_THRE &&
	    (0 == oplus_vooc_get_fastchg_started()) &&
	    (chg_type == OPLUS_CHG_USB_TYPE_PD || chg_type == OPLUS_CHG_USB_TYPE_QC2)) {
		hw_aicl_point = AICL_POINT_VOL_9V;
		sw_aicl_point = AICL_POINT_VOL_9V;
	} else {
		hw_aicl_point = chip->hw_aicl_point;
		sw_aicl_point = chip->sw_aicl_point;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_0; /* 500 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_500MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_500MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		chg_debug("use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 800)
		goto aicl_end;

	i = INPUT_CURRENT_LIMIT_INDEX_1; /* 800 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_800MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_800MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1000)
		goto aicl_end;

	i = INPUT_CURRENT_LIMIT_INDEX_2; /* 1000 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_1000MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_1000MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_3; /* 1200 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_1200MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_1200MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	chg_err("the i=3 chg_vol: %d\n", chg_vol);
	if (chg_vol < sw_aicl_point) {
		i = i - 2; /* We DO NOT use 1.2A here */
		goto aicl_pre_step;
	} else if (current_ma < 1200) {
		goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_4; /* 1500 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_1500MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_1500MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 3; /* We DO NOT use 1.2A here */
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 1; /* We use 1.2A here */
		goto aicl_end;
	} else if (current_ma < 2000) {
		goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_5; /* 1750 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_1750MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_1750MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 3; /* 1.2A */
		goto aicl_pre_step;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_6; /* 2000 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_2000MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_2000MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 3; /* 1.5A */
		goto aicl_pre_step;
	} else if (current_ma < 2200) {
		goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_7; /* 2200 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_2200MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_2200MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 3;
		goto aicl_pre_step;
	} else if (current_ma < 2500) {
		goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_8; /* 2500 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_2500MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_2500MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 3;
		goto aicl_pre_step;
	} else if (current_ma < 2750) {
		goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_9; /* 2750 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_2750MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_2750MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 3;
		goto aicl_pre_step;
	} else if (current_ma < 3000) {
		goto aicl_end;
	}

	i = INPUT_CURRENT_LIMIT_INDEX_10; /* 3000 */
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_3000MA,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_3000MA,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
	msleep(50);
	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 3;
		goto aicl_pre_step;
	}

aicl_pre_step:
	chg_info(
		"usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n",
		chg_vol, i, mp2650_2_usbin_input_current_limit_2[i], sw_aicl_point);
	goto aicl_rerun;
aicl_end:
	chg_info(
		"usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n",
		chg_vol, i, mp2650_2_usbin_input_current_limit_2[i], sw_aicl_point);
	goto aicl_rerun;
aicl_rerun:
	aicl_result = mp2650_2_usbin_input_current_limit_2[i];
	switch (i) {
	case INPUT_CURRENT_LIMIT_INDEX_0:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_500MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_500MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_1:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_800MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_800MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_2:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_1000MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_1000MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_3:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_1200MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_1200MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_4:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_1500MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_1500MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_5:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_1750MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_1750MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_6:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_2000MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_2000MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_7:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_2200MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_2200MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_8:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_2500MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_2500MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_9:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_2750MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_2750MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	case INPUT_CURRENT_LIMIT_INDEX_10:
		rc = mp2650_2_config_interface(
			REG00_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_3000MA,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
		rc = mp2650_2_config_interface(
			REG0F_mp2650_2_ADDRESS,
			REG00_mp2650_2_1ST_CURRENT_LIMIT_3000MA,
			REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
		break;
	default:
		break;
	}

	mp2650_2_set_vindpm_vol_2(hw_aicl_point);
	return rc;
}

int mp2650_2_charging_current_write_fast_2(int chg_cur)
{
	int rc = 0;
	int tmp = 0;
	struct chip_mp2650 *chip = charger_ic_2;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("set charge current = %d\n", chg_cur);

	tmp = chg_cur - REG02_mp2650_2_CHARGE_CURRENT_SETTING_OFFSET;
	tmp = tmp / REG02_mp2650_2_CHARGE_CURRENT_SETTING_STEP;

	rc = mp2650_2_config_interface(
		REG02_mp2650_2_ADDRESS,
		tmp << REG02_mp2650_2_CHARGE_CURRENT_SETTING_SHIFT,
		REG02_mp2650_2_CHARGE_CURRENT_SETTING_MASK);

	return rc;
}

#if 1
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
int mp2650_2_chg_get_dyna_aicl_result_2(void)
{
	return aicl_result;
}
#endif /* CONFIG_OPLUS_SHORT_C_BATT_CHECK */

int mp2650_2_set_aicl_point_2(struct oplus_chg_ic_dev *ic_dev, int vbatt)
{
	struct chip_mp2650 *chip = NULL;
	int chg_type = oplus_wired_get_chg_type();
	int chg_vol = 0;

	if (!ic_dev) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	if (oplus_vooc_get_fastchg_started())
		return 0;

	chg_vol = mp2650_2_get_charger_vol(chip);
	if (chg_vol > AICL_POINT_SWITCH_THRE &&
	    (chg_type == OPLUS_CHG_USB_TYPE_PD || chg_type == OPLUS_CHG_USB_TYPE_QC2)) {
		chip->hw_aicl_point = AICL_POINT_VOL_9V;
		mp2650_2_set_vindpm_vol_2(chip->hw_aicl_point);
	} else if (chip->hw_aicl_point == HW_AICL_POINT_LOW &&
		   vbatt > SWITCH_AICL_POINT_VBAT_HIGH) {
		chip->hw_aicl_point = HW_AICL_POINT_HIGH;
		chip->sw_aicl_point = SW_AICL_POINT_HIGH;
		mp2650_2_set_vindpm_vol_2(chip->hw_aicl_point);
	} else if (chip->hw_aicl_point == HW_AICL_POINT_HIGH &&
		   vbatt < SWITCH_AICL_POINT_VBAT_LOW) {
		chip->hw_aicl_point = HW_AICL_POINT_LOW;
		chip->sw_aicl_point = SW_AICL_POINT_LOW;
		mp2650_2_set_vindpm_vol_2(chip->hw_aicl_point);
	}

	chg_debug("hw_aicl=%d, sw_aicl=%d, chg_vol=%d chg_type=%s\n",
		  chip->hw_aicl_point, chip->sw_aicl_point, chg_vol,
		  oplus_wired_get_chg_type_str(chg_type));

	return 0;
}

int mp2650_2_set_enable_volatile_writes_2(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	/* need do nothing */

	return rc;
}

int mp2650_2_set_complete_charge_timeout_2(int val)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (val == OVERTIME_AC) {
		val = REG09_mp2650_2_CHARGE_TIMER_EN_ENABLE |
		      REG09_mp2650_2_FAST_CHARGE_TIMER_8H;
	} else if (val == OVERTIME_USB) {
		val = REG09_mp2650_2_CHARGE_TIMER_EN_ENABLE |
		      REG09_mp2650_2_FAST_CHARGE_TIMER_12H;
	} else {
		val = REG09_mp2650_2_CHARGE_TIMER_EN_DISABLE |
		      REG09_mp2650_2_FAST_CHARGE_TIMER_20H;
	}

	rc = mp2650_2_config_interface(REG09_mp2650_2_ADDRESS, val,
				     REG09_mp2650_2_FAST_CHARGE_TIMER_MASK |
					     REG09_mp2650_2_CHARGE_TIMER_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn't complete charge timeout rc = %d\n", rc);
	}
	return 0;
}

int mp2650_2_float_voltage_write_2(int vfloat_mv)
{
	int rc = 0;
	int tmp = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("vfloat_mv = %d\n", vfloat_mv);

	if (vfloat_mv > 5000) {
		if (vfloat_mv > 9000) {
			vfloat_mv = 9000;
		}
		vfloat_mv = vfloat_mv / 2;
	}

	if (vfloat_mv > 4500) {
		vfloat_mv = 4500;
	}

	tmp = vfloat_mv * 10 - REG04_mp2650_2_CHARGE_FULL_VOL_OFFSET;
	tmp = tmp / REG04_mp2650_2_CHARGE_FULL_VOL_STEP;

	rc = mp2650_2_config_interface(REG04_mp2650_2_ADDRESS,
				     tmp << REG04_mp2650_2_CHARGE_FULL_VOL_SHIFT,
				     REG04_mp2650_2_CHARGE_FULL_VOL_MASK);

	return rc;
}

int mp2650_2_set_prechg_voltage_threshold_2(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG07_mp2650_2_ADDRESS,
				     REG07_mp2650_2_PRECHARGE_THRESHOLD_7200MV,
				     REG07_mp2650_2_PRECHARGE_THRESHOLD_MASK);

	return 0;
}

int mp2650_2_set_prechg_current_2(int ipre_mA)
{
	int tmp = 0;
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	tmp = ipre_mA - REG03_mp2650_2_PRECHARGE_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG03_mp2650_2_PRECHARGE_CURRENT_LIMIT_STEP;
	rc = mp2650_2_config_interface(
		REG03_mp2650_2_ADDRESS,
		(tmp + 1) << REG03_mp2650_2_PRECHARGE_CURRENT_LIMIT_SHIFT,
		REG03_mp2650_2_PRECHARGE_CURRENT_LIMIT_MASK);

	return 0;
}

int mp2650_2_set_termchg_current_2(int term_curr)
{
	int rc = 0;
	int tmp = 0;

	struct chip_mp2650 *chip = charger_ic_2;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("term_current = %d\n", term_curr);
	tmp = term_curr - REG03_mp2650_2_TERMINATION_CURRENT_LIMIT_OFFSET;
	tmp = tmp / REG03_mp2650_2_TERMINATION_CURRENT_LIMIT_STEP;

	rc = mp2650_2_config_interface(
		REG03_mp2650_2_ADDRESS,
		tmp << REG03_mp2650_2_TERMINATION_CURRENT_LIMIT_SHIFT,
		REG03_mp2650_2_TERMINATION_CURRENT_LIMIT_MASK);
	return 0;
}

int mp2650_2_set_rechg_voltage_2(int recharge_mv)
{
	int rc = 0;
	int tmp = 0;
	struct chip_mp2650 *chip = charger_ic_2;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	tmp = recharge_mv - REG04_mp2650_2_BAT_RECHARGE_THRESHOLD_OFFSET;
	tmp = tmp / REG04_mp2650_2_BAT_RECHARGE_THRESHOLD_STEP;

	/*The rechg voltage is: Charge Full Voltage - 100mV  or - 200mV, default is - 100mV*/
	rc = mp2650_2_config_interface(
		REG04_mp2650_2_ADDRESS,
		tmp << REG04_mp2650_2_BAT_RECHARGE_THRESHOLD_SHIFT,
		REG04_mp2650_2_BAT_RECHARGE_THRESHOLD_MASK);

	if (rc) {
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);
	}
	return rc;
}

int mp2650_2_set_wdt_timer_2(int reg)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG09_mp2650_2_ADDRESS, reg,
				     REG09_mp2650_2_WTD_TIMER_MASK);
	if (rc) {
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);
	}

	return 0;
}

int mp2650_2_set_chging_term_disable_2(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_2_config_interface(REG09_mp2650_2_ADDRESS,
				     REG09_mp2650_2_CHARGE_TERMINATION_EN_DISABLE,
				     REG09_mp2650_2_CHARGE_TERMINATION_EN_MASK);
	if (rc) {
		chg_err("Couldn't set chging term disable rc = %d\n", rc);
	}
	return rc;
}

int mp2650_2_set_chging_term_enable_2(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_2_config_interface(REG09_mp2650_2_ADDRESS,
				     REG09_mp2650_2_CCHARGE_TERMINATION_EN_ENABLE,
				     REG09_mp2650_2_CHARGE_TERMINATION_EN_MASK);
	if (rc) {
		chg_err("Couldn't set chging term enable rc = %d\n", rc);
	}
	return rc;
}

int mp2650_2_kick_wdt_2(struct oplus_chg_ic_dev *ic_dev)
{
	struct chip_mp2650 *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     REG08_mp2650_2_WTD_RST_RESET,
				     REG08_mp2650_2_WTD_RST_MASK);
	if (rc) {
		chg_err("Couldn't mp2650 kick wdt rc = %d\n", rc);
	}
	return rc;
}

int mp2650_2_otg_disable_2(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	rc = mp2650_2_wait_resume(chip);
	if (rc < 0) {
		chg_err("wait resume error, can't disable otg\n");
		return rc;
	}

	mp2650_2_wireless_set_mps_otg_en_val_2(0); /* set disable output 5V vbus */
	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     REG08_mp2650_2_OTG_EN_DISABLE,
				     REG08_mp2650_2_OTG_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_otg_disable_2  rc = %d\n", rc);
	}

	mp2650_2_set_wdt_timer_2(REG09_mp2650_2_WTD_TIMER_40S);

	return rc;
}

int mp2650_2_enable_charging_2(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	mp2650_2_otg_disable_2();
	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     REG08_mp2650_2_CHG_EN_ENABLE,
				     REG08_mp2650_2_CHG_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_enable_charging_2 rc = %d\n", rc);
	}

	chg_err("mp2650_2_enable_charging_2 \n");
	return rc;
}

int mp2650_2_disable_charging_2(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (!chip) {
		return 0;
	}

	chg_err(" mp2650_2_disable_charging_2 \n");

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err(" charger_suspended \n");
		return 0;
	}
	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     REG08_mp2650_2_CHG_EN_DISABLE,
				     REG08_mp2650_2_CHG_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_disable_charging_2  rc = %d\n", rc);
	}

	return rc;
}

int mp2650_2_check_charging_enable_2(void)
{
	int rc = 0;
	int reg_val = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	bool charging_enable = false;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_2_read_reg(REG08_mp2650_2_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG08_mp2650_2_ADDRESS rc = %d\n", rc);
		return 0;
	}

	charging_enable = ((reg_val & REG08_mp2650_2_CHG_EN_MASK) ==
			   REG08_mp2650_2_CHG_EN_ENABLE) ?
					1 :
					0;

	return charging_enable;
}

int mp2650_2_enable_adc_detect_2(bool enable)
{
	int rc;

	chg_err("mp2650_2_enable_adc_detect_2 enable= %d\n", enable);
	/* Enable or disable adc detect */
	if (enable) {
		rc = mp2650_2_config_interface(REG0B_mp2650_2_ADDRESS, 0x3, REG0B_mp2650_2_PROCHOT_PSYS_CFG_MASK);
		if (rc < 0)
			chg_err("Couldn't mp2650_2_enable_adc_detect_2 rc = %d\n", rc);
	} else {
		rc = mp2650_2_config_interface(REG0B_mp2650_2_ADDRESS, 0, REG0B_mp2650_2_PROCHOT_PSYS_CFG_MASK);
		if (rc < 0)
			chg_err("Couldn't mp2650_2_enable_adc_detect_2 rc = %d\n", rc);
	}
	return rc;
}

int mp2650_2_get_vbus_voltage_2(void)
{
	int vol_high = 0;
	int vol_low = 0;
	int vbus_vol = 0;
	int retry = 3;
	struct chip_mp2650 *chip = charger_ic_2;

	if (!chip)
		return 0;

	if (oplus_is_rf_ftm_mode_v2()) {
		mp2650_2_enable_adc_detect_2(true);
		msleep(1);
	}

	mutex_lock(&mp2650_2_i2c_access);
	vbus_vol = i2c_smbus_read_word_data(chip->client, REG1C1D_mp2650_2_ADDRESS);
	if (vbus_vol < 0) {
		while (retry > 0) {
			usleep_range(5000, 5000);
			vbus_vol = i2c_smbus_read_word_data(chip->client, REG1C1D_mp2650_2_ADDRESS);
			if (vbus_vol < 0)
				retry--;
			else
				break;
		}
	}
	mutex_unlock(&mp2650_2_i2c_access);

	if (vbus_vol < 0) {
		chg_err("get vbus_voltage failed: can't read from %02x, vbus_vol = %d\n",
			 REG1C1D_mp2650_2_ADDRESS, vbus_vol);
		return 0;
	}

	vol_low = vbus_vol & REG01_mp2650_2_VINDPM_THRESHOLD_MASK;
	vol_high = (vbus_vol >> 8) & REG01_mp2650_2_VINDPM_THRESHOLD_MASK;
	vbus_vol = (vol_high * 100) + ((vol_low >> 6) * 25);

	chg_debug("vbus_vol = %d \n", vbus_vol);

	if (oplus_is_rf_ftm_mode_v2())
		mp2650_2_enable_adc_detect_2(false);

	return vbus_vol;
}

int mp2650_2_get_ibus_current_2(void)
{
	int cur_high = 0;
	int cur_low = 0;
	int ibus_curr = 0;
	int rc = 0;

	rc = mp2650_2_read_reg(REG1E1F_mp2650_2_ADDRESS, &cur_low);
	if (rc) {
		chg_err("Couldn't read REG1E1F_mp2650_2_ADDRESS rc = %d\n", rc);
		return 0;
	}

	rc = mp2650_2_read_reg((REG1E1F_mp2650_2_ADDRESS + 1), &cur_high);
	if (rc) {
		chg_err("Couldn't read REG1E1F_mp2650_2_ADDRESS + 1 rc = %d\n",
			rc);
		return 0;
	}

	ibus_curr = (cur_high * 25000) + ((cur_low >> 6) * 6250);
	return ibus_curr;
}

int mp2650_2_get_termchg_voltage_2(void)
{
	int vol_data = 0;
	int rc = 0;

	rc = mp2650_2_read_reg(REG04_mp2650_2_ADDRESS, &vol_data);
	if (rc) {
		chg_err("Couldn't read REG04_mp2650_2_ADDRESS rc = %d\n", rc);
		return -1;
	}

	vol_data = (vol_data & REG04_mp2650_2_CHARGE_FULL_VOL_MASK) >>
		   REG04_mp2650_2_CHARGE_FULL_VOL_SHIFT;
	vol_data = vol_data * REG04_mp2650_2_CHARGE_FULL_VOL_STEP;
	vol_data = vol_data + REG04_mp2650_2_CHARGE_FULL_VOL_OFFSET;
	vol_data = vol_data / 10;

	return vol_data;
}

int mp2650_2_get_termchg_current_2(void)
{
	int cur_data = 0;
	int rc = 0;

	rc = mp2650_2_read_reg(REG03_mp2650_2_ADDRESS, &cur_data);
	if (rc) {
		chg_err("Couldn't read REG03_mp2650_2_ADDRESS rc = %d\n", rc);
		return -1;
	}

	cur_data = (cur_data & REG03_mp2650_2_TERMINATION_CURRENT_LIMIT_MASK) >>
		   REG03_mp2650_2_TERMINATION_CURRENT_LIMIT_SHIFT;
	cur_data = cur_data * REG03_mp2650_2_TERMINATION_CURRENT_LIMIT_STEP;
	cur_data = cur_data + REG03_mp2650_2_TERMINATION_CURRENT_LIMIT_OFFSET;

	return cur_data;
}

bool mp2650_2_check_suspend_charger_2(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;
	int data = 0;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_read_reg(REG08_mp2650_2_ADDRESS, &data);
	if (rc) {
		chg_err("Couldn't read REG1C1D_mp2650_2_ADDRESS rc = %d\n", rc);
		return 0;
	}

	if (data & REG08_mp2650_2_LEARN_EN_MASK) {
		return true;
	} else {
		return false;
	}

	return rc;
}

int mp2650_2_suspend_charger_2(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (!chip) {
		return 0;
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				REG08_mp2650_2_LEARN_EN_ENABLE,
				REG08_mp2650_2_LEARN_EN_MASK);

	chg_err(" rc = %d\n", rc);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_suspend_charger_2 rc = %d\n", rc);
	}

	return rc;
}
int mp2650_2_unsuspend_charger_2(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				REG08_mp2650_2_LEARN_EN_DISABLE,
				REG08_mp2650_2_LEARN_EN_MASK);

	chg_err(" rc = %d\n", rc);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_unsuspend_charger_2 rc = %d\n", rc);
	}
	return rc;
}

bool mp2650_2_check_charger_resume(void)
{
	struct chip_mp2650 *chip = charger_ic_2;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return false;
	} else {
		return true;
	}
}

int mp2650_2_reset_charger_for_wired_charge(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("reset mp2650 for wired charge! \n");

	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     REG08_mp2650_2_reg_2_RST_RESET,
				     REG08_mp2650_2_reg_2_RST_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_reset_charger  rc = %d\n", rc);
	}

	return rc;
}

int mp2650_2_otg_ilim_set(int ilim)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_2_config_interface(REG07_mp2650_2_ADDRESS, ilim,
				     REG07_mp2650_2_OTG_CURRENT_LIMIT_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_otg_ilim_set  rc = %d\n", rc);
	}

	return rc;
}

static int mp2650_2_push_buck_err(struct oplus_chg_ic_dev *ic_dev,
				int reg_value, bool i2c_err, int retry)
{
	oplus_chg_ic_creat_err_msg(ic_dev,
			OPLUS_IC_ERR_BUCK_BOOST, 0,
			"REG3F[%x],i2c_err[%d],retry[%d]",
			 reg_value, i2c_err, retry);
	oplus_chg_ic_virq_trigger(ic_dev, OPLUS_IC_VIRQ_ERR);
	return 0;
}

#define mp2650_2_MAX_SEND_COUNT   10
#define mp2650_2_OTG_MAX_RETRY	3
int mp2650_2_otg_enable(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;
	int retry = 0;
	int reg_value = 0;
	bool i2c_err = false;
	static int send_count = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	rc = mp2650_2_wait_resume(chip);
	if (rc < 0) {
		chg_err("wait resume error, can't enable otg\n");
		return rc;
	}

	chg_info("otg enable\n");
	rc = mp2650_2_burst_mode_enable(true);

	mp2650_2_set_wdt_timer_2(REG09_mp2650_2_WTD_TIMER_DISABLE);

	mutex_lock(&mp2650_2_i2c_access);
	rc = mp2650_2_config_interface_without_lock(REG53_mp2650_2_ADDRESS, 0x95, 0xff);
	rc = mp2650_2_config_interface_without_lock(REG3F_mp2650_2_ADDRESS, 0x20, 0xff);
	rc = mp2650_2_config_interface_without_lock(REG53_mp2650_2_ADDRESS, 0x00, 0xff);
	mutex_unlock(&mp2650_2_i2c_access);

	mp2650_2_wireless_set_mps_otg_en_val_2(1); /* set output 5V vbus */

	rc = mp2650_2_otg_ilim_set(REG07_mp2650_2_OTG_CURRENT_LIMIT_1250MA);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_otg_ilim_set rc = %d\n", rc);
	}
	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     REG08_mp2650_2_OTG_EN_ENABLE,
				     REG08_mp2650_2_OTG_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_otg_enable  rc = %d\n", rc);
	}

	msleep(10);

	/* Note: need to clear the 3F bit5, otherwise the charger maybe charging less than 1ms */
	mutex_lock(&mp2650_2_i2c_access);
	while (retry++ < mp2650_2_OTG_MAX_RETRY) {
		rc = mp2650_2_config_interface_without_lock(REG53_mp2650_2_ADDRESS, 0x95, 0xff);
		if (rc < 0) {
			chg_err("Couldn't set the REG53, rc = %d\n", rc);
			i2c_err = true;
			continue;
		}

		rc = mp2650_2_config_interface_without_lock(REG3F_mp2650_2_ADDRESS, 0x00, 0xff);
		if (rc < 0) {
			chg_err("Couldn't clear the REG3F, rc = %d\n", rc);
			i2c_err = true;
			continue;
		} else {
			rc = __mp2650_2_read_reg(REG3F_mp2650_2_ADDRESS, &reg_value);

			/* read back value of REG3F shall be 0x00 */
			if (rc >= 0 && reg_value == 0) {
				chg_info(" after %d times, clear the REG3F success!\n", retry);
				break;
			} else {
				chg_err("read back reg_value = 0x%x, rc = %d, clear failed\n", reg_value, rc);
				if (rc < 0)
					i2c_err = true;
				continue;
			}
		}
	}
	rc = mp2650_2_config_interface_without_lock(REG53_mp2650_2_ADDRESS, 0x00, 0xff);
	mutex_unlock(&mp2650_2_i2c_access);

	if (reg_value != 0 || retry > 1) {
		chg_err("read back reg_value = 0x%x, i2c_err = %d, retry = %d, send_count = %d\n",
			 reg_value, i2c_err, retry, send_count);
		if (send_count++ <= mp2650_2_MAX_SEND_COUNT)
			mp2650_2_push_buck_err(chip->ic_dev, reg_value, i2c_err, retry);
	}

	return rc;
}

int mp2650_2_otg_wait_vbus_decline(void)
{
#if 1
	msleep(200);
	return 0;
#else
	int rc;
	int vbus_volt = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("<~WPC~>mp2650_2_otg_wait_vbus_decline\n");

	do {
		msleep(200);
		vbus_volt = mp2650_2_get_vbus_voltage_2();
	} while (vbus_volt >= 3200);

	return rc;
#endif
}

int mp2650_2_reset_charger(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (chip->probe_flag) {
		rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
					     REG08_mp2650_2_reg_2_RST_RESET,
					     REG08_mp2650_2_reg_2_RST_MASK);
		if (rc < 0) {
			chg_err("Couldn't mp2650_2_reset_charger  rc = %d\n", rc);
		}
		chip->probe_flag = false;
		chg_err("HW reset chip->probe_flag = %d\n", chip->probe_flag);
	} else {
		rc = mp2650_2_config_interface(REG10_mp2650_2_ADDRESS, 0x01, 0xFF);
		rc = mp2650_2_config_interface(REG11_mp2650_2_ADDRESS, 0xFF, 0xFF);
		rc = mp2650_2_config_interface(REG12_mp2650_2_ADDRESS, 0x77, 0xFF);
		rc = mp2650_2_config_interface(REG2D_mp2650_2_ADDRESS, 0x0F, 0xFF);
		rc = mp2650_2_config_interface(REG36_mp2650_2_ADDRESS, 0xF5, 0xFF);
		rc = mp2650_2_burst_mode_enable(true);

		rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS, 0, 0xFF);
		rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS, 0, 0xFF);

		rc = mp2650_2_config_interface(REG01_mp2650_2_ADDRESS, 0x2D, 0xFF);
		rc = mp2650_2_config_interface(REG02_mp2650_2_ADDRESS, 0X14, 0xFF);
		rc = mp2650_2_config_interface(REG03_mp2650_2_ADDRESS, 0x32, 0xFF);
		rc = mp2650_2_config_interface(REG04_mp2650_2_ADDRESS, 0x4E, 0xFF);
		rc = mp2650_2_config_interface(REG05_mp2650_2_ADDRESS, 0x03, 0xFF);
		rc = mp2650_2_config_interface(REG06_mp2650_2_ADDRESS, 0x5, 0xFF);
		rc = mp2650_2_config_interface(REG07_mp2650_2_ADDRESS, 0x14, 0xFF);
		rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS, 0x16, 0xFF);
		rc = mp2650_2_config_interface(REG09_mp2650_2_ADDRESS, 0x4C, 0xFF);
		rc = mp2650_2_config_interface(REG0A_mp2650_2_ADDRESS, 0x36, 0xFF);
		rc = mp2650_2_config_interface(REG0B_mp2650_2_ADDRESS, 0, 0xFF);
		rc = mp2650_2_config_interface(REG0C_mp2650_2_ADDRESS, 0xC0, 0xFF);
		rc = mp2650_2_config_interface(REG0D_mp2650_2_ADDRESS, 0x01, 0xFF);
		rc = mp2650_2_config_interface(REG0E_mp2650_2_ADDRESS, 0x23, 0xFF);
		rc = mp2650_2_config_interface(REG31_mp2650_2_ADDRESS, 0x03, 0xFF);
		rc = mp2650_2_config_interface(REG33_mp2650_2_ADDRESS, 0x15, 0xFF);
		/*MP2762A VSYS Ripple Issue 20200730, it occurs after charging full and disable charge*/
		rc = mp2650_2_config_interface(REG2D_mp2650_2_ADDRESS, 0x0f, 0xff);
		chg_err(" SW reset chip->probe_flag = %d\n", chip->probe_flag);
	}

	return rc;
}

#ifdef OPLUS_CHG_UNDEF
int mp2650_2_otg_vlim_set(int vlim)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	return rc;
}
#endif

int mp2650_2_set_switching_frequency(void)
{
	/* int value; */
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG0B_mp2650_2_ADDRESS,
				     REG0B_mp2650_2_SW_FREQ_800K,
				     REG0B_mp2650_2_SW_FREQ_MASK);
	return 0;
}

int mp2650_2_set_prochot_psys_cfg(void)
{
	/* int value; */
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG0B_mp2650_2_ADDRESS,
				     REG0B_mp2650_2_PROCHOT_PSYS_CFG_DISABLE,
				     REG0B_mp2650_2_PROCHOT_PSYS_CFG_MASK);
	return 0;
}

int mp2650_2_set_mps_otg_current(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG07_mp2650_2_ADDRESS,
				     REG07_mp2650_2_OTG_CURRENT_LIMIT_1250MA,
				     REG07_mp2650_2_OTG_CURRENT_LIMIT_MASK);
	return 0;
}

int mp2650_2_set_mps_otg_voltage(bool is_9v)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (is_9v) {
		rc = mp2650_2_config_interface(REG06_mp2650_2_ADDRESS,
					     REG06_mp2650_2_OTG_VOL_OPTION_8750MV,
					     REG06_mp2650_2_OTG_VOL_OPTION_MASK);
	} else {
		rc = mp2650_2_config_interface(REG06_mp2650_2_ADDRESS,
					     REG06_mp2650_2_OTG_VOL_OPTION_4750MV,
					     REG06_mp2650_2_OTG_VOL_OPTION_MASK);
	}
	return 0;
}

int mp2650_2_set_mps_second_otg_voltage(bool is_750mv)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (is_750mv) {
		rc = mp2650_2_config_interface(
			REG06_mp2650_2_ADDRESS,
			REG06_mp2650_2_2ND_OTG_VOL_SETTING_400MV,
			REG06_mp2650_2_2ND_OTG_VOL_SETTING_MASK);
	} else {
		rc = mp2650_2_config_interface(
			REG06_mp2650_2_ADDRESS,
			REG06_mp2650_2_2ND_OTG_VOL_SETTING_250MV,
			REG06_mp2650_2_2ND_OTG_VOL_SETTING_MASK);
	}
	return 0;
}

int mp2650_2_set_mps_otg_enable(void)
{
	/* int value; */
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("mp2650_2_set_mps_otg_enable start \n");
	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     REG08_mp2650_2_OTG_EN_ENABLE,
				     REG08_mp2650_2_OTG_EN_MASK);
	return 0;
}

int mp2650_2_set_mps_otg_disable(void)
{
	/* int value; */
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_err("mp2650_2_set_mps_otg_disable start \n");
	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     REG08_mp2650_2_OTG_EN_DISABLE,
				     REG08_mp2650_2_OTG_EN_MASK);
	return 0;
}

int mp2650_2_enable_hiz(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     REG08_mp2650_2_LEARN_EN_ENABLE,
				     REG08_mp2650_2_LEARN_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn'mp2650_2_enable_hiz rc = %d\n", rc);
	}

	chg_err("mp2650_2_enable_hiz \n");

	return rc;
}

int mp2650_2_disable_hiz(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     REG08_mp2650_2_LEARN_EN_DISABLE,
				     REG08_mp2650_2_LEARN_EN_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_disable_hiz  rc = %d\n", rc);
	}

	chg_err("mp2650_2_disable_hiz \n");

	return rc;
}

int mp2650_2_enable_buck_switch(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG12_mp2650_2_ADDRESS,
				     REG12_mp2650_2_BUCK_SWITCH_ENABLE,
				     REG12_mp2650_2_BUCK_SWITCH_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_enable_dig_skip rc = %d\n", rc);
	}

	chg_err("mp2650_2_enable_dig_skip \n");

	return rc;
}

int mp2650_2_disable_buck_switch(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG12_mp2650_2_ADDRESS,
				     REG12_mp2650_2_BUCK_SWITCH_DISABLE,
				     REG12_mp2650_2_BUCK_SWITCH_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_disable_dig_skip  rc = %d\n", rc);
	}

	chg_err("mp2650_2_disable_dig_skip \n");

	return rc;
}

int mp2650_2_enable_async_mode(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG36_mp2650_2_ADDRESS,
				     REG36_mp2650_2_ASYNC_MODE_ENABLE,
				     REG36_mp2650_2_ASYNC_MODE_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_enable_async_mode rc = %d\n", rc);
	}

	chg_err("mp2650_2_enable_async_mode\n");

	return rc;
}

int mp2650_2_disable_async_mode(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG36_mp2650_2_ADDRESS,
				     REG36_mp2650_2_ASYNC_MODE_DISABLE,
				     REG36_mp2650_2_ASYNC_MODE_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_disable_async_mode rc = %d\n", rc);
	}

	chg_err("mp2650_2_disable_async_mode\n");

	return rc;
}

static int mp2650_2_vbus_avoid_electric_config(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	return 0;
	chg_err("mp2650_2_vbus_avoid_electric_config start\n");
	rc = mp2650_2_config_interface(REG53_mp2650_2_ADDRESS, 0x95, 0xff);
	rc = mp2650_2_config_interface(REG39_mp2650_2_ADDRESS, 0x40, 0xff);
	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS, 0x36, 0xff);
	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS, 0x16, 0xff);
	rc = mp2650_2_config_interface(REG39_mp2650_2_ADDRESS, 0x00, 0xff);
	rc = mp2650_2_config_interface(REG53_mp2650_2_ADDRESS, 0x00, 0xff);
	rc = mp2650_2_config_interface(REG2F_mp2650_2_ADDRESS, 0x15, 0xff);
	return 0;
}

int mp2650_2_check_learn_mode(void)
{
	int rc = 0;
	int reg_val = 0;
	bool learn_mode = false;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = mp2650_2_read_reg(REG08_mp2650_2_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG08_mp2650_2_ADDRESS rc = %d\n", rc);
		return 0;
	}

	chg_err(" LEARN MODE = %02x\n", reg_val);

	learn_mode = ((reg_val & REG08_mp2650_2_LEARN_EN_MASK) ==
		      REG08_mp2650_2_LEARN_EN_MASK) ?
				   1 :
				   0;

	return learn_mode;
}

static int mp2650_2_set_charger_vsys_threshold(struct chip_mp2650 *chip, int val)
{
	int rc;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	/* change Vsys Skip threshold */
	rc = mp2650_2_config_interface(REG31_mp2650_2_ADDRESS, val, 0xff);

	return rc;
}

/*this function just can use mp2650_2_config_interface_without_lock to modify registers */
static int mp2650_2_burst_mode_enable(bool enable)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	mutex_lock(&mp2650_2_i2c_access);
	chg_info("%s burst mode\n", enable ? "enable" : "disable");
	/* Enable or disable Burst mode */
	if (enable) {
		rc = mp2650_2_config_interface_without_lock(REG53_mp2650_2_ADDRESS,
							  0x95, 0xff);
		if (rc != 0)
			goto error;
		rc = mp2650_2_config_interface_without_lock(REG3E_mp2650_2_ADDRESS,
							  0x00, 0xff);
		if (rc != 0)
			goto error;
		rc = mp2650_2_config_interface_without_lock(REG53_mp2650_2_ADDRESS,
							  0x00, 0xff);
		if (rc != 0)
			goto error;
	} else {
		rc = mp2650_2_config_interface_without_lock(REG53_mp2650_2_ADDRESS,
							  0x95, 0xff);
		if (rc != 0)
			goto error;
		rc = mp2650_2_config_interface_without_lock(REG3E_mp2650_2_ADDRESS,
							  0x80, 0xff);
		if (rc != 0)
			goto error;
		rc = mp2650_2_config_interface_without_lock(REG53_mp2650_2_ADDRESS,
							  0x00, 0xff);
		if (rc != 0)
			goto error;
	}
error:
	mutex_unlock(&mp2650_2_i2c_access);
	return rc;
}

int mp2650_2_other_registers_init_2(void)
{
	int rc;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	/* add for mp2762 damaged issue,keep Ilimi2 duration shortest */
	rc = mp2650_2_config_interface(REG10_mp2650_2_ADDRESS, 0x01, 0xff);
	rc = mp2650_2_config_interface(REG11_mp2650_2_ADDRESS, 0xff, 0xff);

	return rc;
}

#define DUMP_REG_LOG_CNT_30S 6
void mp2650_2_dump_registers_2(void)
{
	int rc;
	int addr;
	static int dump_count = 0;
	struct chip_mp2650 *chip = charger_ic_2;
	unsigned int val_buf[mp2650_2_DUMP_MAX_REG + 5] = { 0x0 };

	if (atomic_read(&chip->charger_suspended) == 1) {
		return;
	}

	if (dump_count == DUMP_REG_LOG_CNT_30S) {
		dump_count = 0;
		for (addr = mp2650_2_FIRST_REG; addr <= mp2650_2_DUMP_MAX_REG;
		     addr++) {
			rc = mp2650_2_read_reg(addr, &val_buf[addr]);
			if (rc) {
				chg_err("Couldn't read 0x%02x rc = %d\n", addr,
					rc);
			}
		}
		rc = mp2650_2_read_reg(0x48, &val_buf[mp2650_2_DUMP_MAX_REG + 1]);
		if (rc) {
			chg_err("Couldn't  read 0x48 rc = %d\n", rc);
		}
		rc = mp2650_2_read_reg(0x49, &val_buf[mp2650_2_DUMP_MAX_REG + 2]);
		if (rc) {
			chg_err("Couldn't  read 0x49 rc = %d\n", rc);
		}
		rc = mp2650_2_read_reg(0x2D, &val_buf[mp2650_2_DUMP_MAX_REG + 3]);
		if (rc) {
			chg_err("Couldn't  read 0x2D rc = %d\n", rc);
		}
		rc = mp2650_2_read_reg(0x30, &val_buf[mp2650_2_DUMP_MAX_REG + 4]);
		if (rc) {
			chg_err("Couldn't  read 0x30 rc = %d\n", rc);
		}

		printk(KERN_ERR
		       "mp2650_2_dump_reg: [0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
		       "[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
		       "[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
		       "[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
		       "[0x%02x, 0x%02x, 0x%02x], [reg48=0x%02x, reg49=0x%02x, reg2D=0x%02x, reg30=0x%02x]\n",
		       val_buf[0], val_buf[1], val_buf[2], val_buf[3],
		       val_buf[4], val_buf[5], val_buf[6], val_buf[7],
		       val_buf[8], val_buf[9], val_buf[10], val_buf[11],
		       val_buf[12], val_buf[13], val_buf[14], val_buf[15],
		       val_buf[16], val_buf[17], val_buf[18], val_buf[19],
		       val_buf[20], val_buf[21], val_buf[22], val_buf[23],
		       val_buf[24], val_buf[25], val_buf[26], val_buf[27],
		       val_buf[28], val_buf[29], val_buf[30], val_buf[31],
		       val_buf[32], val_buf[33], val_buf[34], val_buf[35],
		       val_buf[36], val_buf[37], val_buf[38]);
	} else {
		mp2650_2_reg_2_dump_internal();
	}
	dump_count++;
}

bool mp2650_2_need_to_check_ibatt_2(void)
{
	return false;
}

int mp2650_2_get_chg_current_step_2(void)
{
	int rc = 50;

	return rc;
}

int mp2650_2_input_current_limit_init_2(void)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("mp2650_2_input_current_limit_init_2: in suspended\n");
		return 0;
	}
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS, 0x03,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS, 0x03,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);

	if (rc < 0) {
		chg_err("Couldn't mp2650_2_input_current_limit_init_2 rc = %d\n",
			rc);
	}

	return rc;
}

static int mp2650_2_input_current_limit_without_aicl(struct chip_mp2650 *chip, int current_ma)
{
	int rc = 0;
	int reg_val = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("mp2650_2_input_current_limit_init_2: in suspended\n");
		return 0;
	}
	chip->pre_current_ma = current_ma;

	reg_val = current_ma / REG00_mp2650_2_1ST_CURRENT_LIMIT_STEP;
	chg_err(" reg_val current [%d]ma\n", current_ma);
	rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS, reg_val,
				     REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
	rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS, reg_val,
				     REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);

	if (rc < 0) {
		chg_err("Couldn't mp2650_2_input_current_limit_init_2 rc = %d\n",
			rc);
	}

	return rc;
}

int mp2650_2_set_voltage_slew_rate_2(int value)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("mp2650_2_set_voltage_slew_rate_2: in suspended\n");
		return 0;
	}
	if (value == 1)
		rc = mp2650_2_config_interface(0x2D, 0x0E,
					     BIT(3) | BIT(2) | BIT(1));
	else if (value == 2)
		rc = mp2650_2_config_interface(0x2D, 0x06,
					     BIT(3) | BIT(2) | BIT(1));
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_input_current_limit_init_2 rc = %d\n",
			rc);
	}
	return 0;
}

static int mp2650_2_set_option(int option)
{
	int rc = 0;
	struct chip_mp2650 *chip = charger_ic_2;

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("mp2650_2_set_voltage_slew_rate_2: in suspended\n");
		return 0;
	}
	if (option == 1)
		rc = mp2650_2_config_interface(REG30_mp2650_2_ADDRESS, 0,
					     REG30_mp2650_2_OPTION_MASK);
	else if (option == 2)
		rc = mp2650_2_config_interface(REG30_mp2650_2_ADDRESS, 1,
					     REG30_mp2650_2_OPTION_MASK);
	if (rc < 0) {
		chg_err("Couldn't mp2650_2_set_option rc = %d\n", rc);
	}
	return 0;
}

int mp2650_2_hardware_init_2(void)
{
	struct chip_mp2650 *chip = charger_ic_2;

	chg_err("init mp2650 hardware! \n");

	/* must be before set_vindpm_vol and set_input_current */
	chip->hw_aicl_point = HW_AICL_POINT_LOW;
	chip->sw_aicl_point = SW_AICL_POINT_LOW;

	mp2650_2_reset_charger();

	mp2650_2_disable_charging_2();

	mp2650_2_set_chging_term_disable_2();

	mp2650_2_input_current_limit_init_2();

	mp2650_2_float_voltage_write_2(WPC_TERMINATION_VOLTAGE);

	mp2650_2_otg_ilim_set(REG07_mp2650_2_OTG_CURRENT_LIMIT_1250MA);

	mp2650_2_set_enable_volatile_writes_2();

	mp2650_2_set_complete_charge_timeout_2(OVERTIME_DISABLED);

	mp2650_2_set_option(1);

	mp2650_2_set_prechg_voltage_threshold_2();

	mp2650_2_set_prechg_current_2(WPC_PRECHARGE_CURRENT);

	mp2650_2_charging_current_write_fast_2(WPC_CHARGE_CURRENT_DEFAULT);

	mp2650_2_set_termchg_current_2(WPC_TERMINATION_CURRENT);

	mp2650_2_set_rechg_voltage_2(WPC_RECHARGE_VOLTAGE_OFFSET);

	mp2650_2_set_switching_frequency();

	mp2650_2_set_prochot_psys_cfg();

	mp2650_2_set_vindpm_vol_2(chip->hw_aicl_point);

	mp2650_2_set_mps_otg_current();

	mp2650_2_set_mps_otg_voltage(false);
	mp2650_2_set_mps_second_otg_voltage(false);

	mp2650_2_set_mps_otg_enable();

	mp2650_2_other_registers_init_2();

	mp2650_2_unsuspend_charger_2();

	mp2650_2_enable_charging_2();

	mp2650_2_set_wdt_timer_2(REG09_mp2650_2_WTD_TIMER_40S);

	mp2650_2_input_current_limit_without_aicl(chip, 500);

	return true;
}

#ifdef CONFIG_OPLUS_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		chg_err("%s: unable to open rtc device (%s)\n", __FILE__,
		       CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		chg_err("Error reading rtc device (%s) : %d\n",
		       CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		chg_err("Invalid RTC time (%s): %d\n", CONFIG_RTC_HCTOSYS_DEVICE,
		       rc);
		goto close_time;
	}

	if ((tm.tm_year == 110) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(
			": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon,
			tm.tm_year, tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(
		": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon,
		tm.tm_year, tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPLUS_RTC_DET_SUPPORT */

void __attribute__((weak)) oplus_set_typec_sinkonly(void)
{
	return;
}

bool __attribute__((weak)) oplus_usbtemp_condition(void)
{
	return false;
}

static void register_charger_devinfo(void)
{
	int ret = 0;
	char *version;
	char *manufacture;

	version = "mp2650";
	manufacture = "MP";

	ret = register_device_proc("charger", version, manufacture);
	if (ret)
		chg_err("register_charger_devinfo fail\n");
}

bool oplus_charger_ic_2_chip_is_null(void)
{
	if (!charger_ic_2) {
		return true;
	} else {
		return false;
	}
}

static int mp2650_2_mps_otg_en_gpio_init(struct chip_mp2650 *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip_mp2650 not ready!\n",
		       __func__);
		return -EINVAL;
	}

	chip->mps_otg_en_active =
		pinctrl_lookup_state(chip->pinctrl, "mps_otg_en_active");
	if (IS_ERR_OR_NULL(chip->mps_otg_en_active)) {
		chg_err("get mps_otg_en_active fail\n");
		return -EINVAL;
	}

	chip->mps_otg_en_sleep =
		pinctrl_lookup_state(chip->pinctrl, "mps_otg_en_sleep");
	if (IS_ERR_OR_NULL(chip->mps_otg_en_sleep)) {
		chg_err("get mps_otg_en fail\n");
		return -EINVAL;
	}

	chip->mps_otg_en_default =
		pinctrl_lookup_state(chip->pinctrl, "mps_otg_en_default");
	if (IS_ERR_OR_NULL(chip->mps_otg_en_default)) {
		chg_err("get mps_otg_en_default fail\n");
		return -EINVAL;
	}

	/* if (chg->otg_en_gpio > 0) { */
	/* gpio_direction_input(chg->wired_conn_gpio); */
	/* } */

	pinctrl_select_state(chip->pinctrl, chip->mps_otg_en_sleep);
	chg_err("gpio_val:%d\n", gpio_get_value(chip->mps_otg_en_gpio));

	return 0;
}

static void mp2650_2_plugin_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_mp2650 *chip =
		container_of(dwork, struct chip_mp2650, plugin_work);
	int tmp1, tmp2;

	if (gpio_is_valid(chip->acok_gpio)) {
		tmp1 = gpio_get_value(chip->acok_gpio);
re_check:
		usleep_range(10000, 10001);
		tmp2 = gpio_get_value(chip->acok_gpio);
		if (tmp1 == tmp2) {
			chip->acok = !tmp2;
			oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_PLUGIN);
		} else {
			tmp1 = tmp2;
			goto re_check;
		}
	}
}

static irqreturn_t mp2650_2_acok_handler(int irq, void *dev_id)
{
	struct chip_mp2650 *chip = dev_id;

	chg_debug("mps acok irq\n");
	schedule_delayed_work(&chip->plugin_work, msecs_to_jiffies(5));
	return IRQ_HANDLED;
}

static int mp2650_2_gpio_init(struct chip_mp2650 *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get mp2650 pinctrl fail\n");
		return -EINVAL;
	}

	/* Parsing gpio mps_otg_en */
	chip->mps_otg_en_gpio =
		of_get_named_gpio(node, "oplus,mps_otg_en-gpio", 0);
	if (chip->mps_otg_en_gpio < 0) {
		chg_err("chip->mps_otg_en_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->mps_otg_en_gpio)) {
			rc = gpio_request(chip->mps_otg_en_gpio,
					  "mps_otg_en-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n",
				       chip->mps_otg_en_gpio);
				return rc;
			}
		}
		rc = mp2650_2_mps_otg_en_gpio_init(chip);
		if (rc < 0) {
			chg_err("mps otg gpio init error, rc=%d\n", rc);
			goto free_otg_gpio;
		}
	}

	chip->acok_gpio = of_get_named_gpio(node, "oplus,mps_acok-gpio", 0);
	if (gpio_is_valid(chip->acok_gpio)) {
		rc = gpio_request(chip->acok_gpio, "mps_acok-gpio");
		if (rc < 0) {
			chg_err("unable to request gpio [%d], rc=%d\n",
				chip->acok_gpio, rc);
			goto free_otg_gpio;
		}

		chip->acok_default =
			pinctrl_lookup_state(chip->pinctrl, "mps_acok_default");
		if (IS_ERR_OR_NULL(chip->acok_default)) {
			chg_err("get mps_acok_default fail\n");
			goto free_acok_gpio;
		}
		gpio_direction_input(chip->acok_gpio);
		pinctrl_select_state(chip->pinctrl, chip->acok_default);
		chip->acok_irq = gpio_to_irq(chip->acok_gpio);
		rc = devm_request_irq(chip->dev, chip->acok_irq,
				      mp2650_2_acok_handler,
				      IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				      "mps_acok-irq", chip);
		if (rc < 0) {
			chg_err("mps acok_irq request error, rc=%d\n", rc);
			goto free_acok_gpio;
		}
		enable_irq_wake(chip->acok_irq);
	}

	return 0;

free_acok_gpio:
	if (gpio_is_valid(chip->acok_gpio))
		gpio_free(chip->acok_gpio);
free_otg_gpio:
	if (gpio_is_valid(chip->mps_otg_en_gpio))
		gpio_free(chip->mps_otg_en_gpio);
	return rc;
}

void mp2650_2_wireless_set_mps_otg_en_val_2(int value)
{
	struct chip_mp2650 *chip = charger_ic_2;
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip_mp2650 not ready!\n",
		       __func__);
		return;
	}

	if (chip->mps_otg_en_gpio <= 0) {
		chg_err("mps_otg_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->mps_otg_en_active) ||
	    IS_ERR_OR_NULL(chip->mps_otg_en_sleep) ||
	    IS_ERR_OR_NULL(chip->mps_otg_en_default)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(chip->mps_otg_en_gpio, 1);
		pinctrl_select_state(chip->pinctrl, chip->mps_otg_en_active);
	} else {
		gpio_direction_output(chip->mps_otg_en_gpio, 0);
		pinctrl_select_state(chip->pinctrl, chip->mps_otg_en_default);
	}

	chg_err("<~WPC~>set value:%d, gpio_val:%d\n", value,
		gpio_get_value(chip->mps_otg_en_gpio));
}

int mp2650_2_wireless_get_mps_otg_en_val_2(void)
{
	struct chip_mp2650 *chip = charger_ic_2;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip_mp2650 not ready!\n",
		       __func__);
		return -1;
	}

	if (chip->mps_otg_en_gpio <= 0) {
		chg_err("mps_otg_en_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->mps_otg_en_active) ||
	    IS_ERR_OR_NULL(chip->mps_otg_en_sleep) ||
	    IS_ERR_OR_NULL(chip->mps_otg_en_default)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	return gpio_get_value(chip->mps_otg_en_gpio);
}

#ifdef DEBUG_BY_FILE_OPS
static char mp2650_2_add = 0;

/*echo "xx" > /proc/mp2650_2_write_log*/
static ssize_t mp2650_2_data_log_write(struct file *filp, const char __user *buff,
				     size_t len, loff_t *data)
{
	char write_data[32] = { 0 };
	int critical_log = 0;
	char *e;
	int rc;

	if ((len >= sizeof(write_data)) || (len < 1))
		return -EINVAL;

	if (copy_from_user(write_data, buff, len)) {
		chg_err("mp2650_2_data_log_write error.\n");
		return -EFAULT;
	}

	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	if (!strncmp(write_data, "0x", 2)) {
		critical_log = (int)simple_strtoul(write_data, &e, 16);
	} else if (isdigit(*write_data)) {
		critical_log = (int)simple_strtoul(write_data, &e, 10);
	} else {
		chg_err("input data format error");
		return -EINVAL;
	}
	if (write_data == e || *e != '\0') {
		chg_err("input data format error, conversion not complete");
		return -EINVAL;
	}
	if (critical_log > 256)
		critical_log = 256;

	chg_err("input data = %s,  write_mp2650_2_data = 0x%02X\n",
		write_data, critical_log);

	rc = mp2650_2_config_interface(mp2650_2_add, critical_log, 0xff);
	if (rc < 0)
		chg_err("Couldn't write 0x%02X rc = %d\n", mp2650_2_add, rc);

	return len;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations mp2650_2_write_log_proc_fops = {
	.write  = mp2650_2_data_log_write,
	.llseek = noop_llseek,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops mp2650_2_write_log_proc_fops = {
	.proc_write = mp2650_2_data_log_write,
	.proc_lseek = noop_llseek,
};
#endif

static void init_mp2650_2_write_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("mp2650_2_write_log", 0664, NULL,
			&mp2650_2_write_log_proc_fops);
	if (!p) {
		chg_err("proc_create mp2650_2_write_log fail!\n");
	}
}

/*echo "xx" > /proc/mp2650_2_read_log*/
static ssize_t mp2650_2_reg_2_store(struct file *filp, const char __user *buff,
				size_t len, loff_t *data)
{
	char write_data[32] = { 0 };
	int critical_log = 0;
	char *e;
	int rc;
	int val_buf;

	if ((len >= sizeof(write_data)) || (len < 1))
		return -EINVAL;

	if (copy_from_user(write_data, buff, len)) {
		chg_err("mp2650_2_data_log_read error.\n");
		return -EFAULT;
	}

	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	if (!strncmp(write_data, "0x", 2)) {
		critical_log = (int)simple_strtoul(write_data, &e, 16);
	} else if (isdigit(*write_data)) {
		critical_log = (int)simple_strtoul(write_data, &e, 10);
	} else {
		chg_err("input data format error");
		return -EINVAL;
	}
	if (write_data == e || *e != '\0') {
		chg_err("input data format error, conversion not complete");
		return -EINVAL;
	}
	if (critical_log > 256)
		critical_log = 256;

	mp2650_2_add = critical_log;

	chg_err("input data = %s,  mp2650_2_addr = 0x%02X\n",
		write_data, mp2650_2_add);

	rc = mp2650_2_read_reg(mp2650_2_add, &val_buf);
	if (rc)
		chg_err("Couldn't read 0x%02X rc = %d\n", mp2650_2_add, rc);
	else
		chg_info("mp2650_2_read 0x%02X = 0x%02X\n", mp2650_2_add, val_buf);

	return len;
}

static ssize_t mp2650_2_reg_2_show(struct file *filp, char __user *buff,
			       size_t count, loff_t *off)
{
	char page[256] = { 0 };
	int rc;
	int val_buf;
	int len = 0;

	rc = mp2650_2_read_reg(mp2650_2_add, &val_buf);
	if (rc) {
		chg_err("Couldn't read 0x%02X rc = %d\n", mp2650_2_add, rc);
	}

	len = sprintf(page, "reg = 0x%x, data = 0x%x\n", mp2650_2_add, val_buf);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}

	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}

	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations mp2650_2_read_log_proc_fops = {
	.read = mp2650_2_reg_2_show,
	.write  = mp2650_2_reg_2_store,
	.llseek = noop_llseek,
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops mp2650_2_read_log_proc_fops = {
	.proc_read = mp2650_2_reg_2_show,
	.proc_write = mp2650_2_reg_2_store,
	.proc_lseek = noop_llseek,
};
#endif

static void init_mp2650_2_read_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("mp2650_2_read_log", 0664, NULL,
			&mp2650_2_read_log_proc_fops);
	if (!p) {
		chg_err("proc_create mp2650_2_read_log fail!\n");
	}
}
#endif /*DEBUG_BY_FILE_OPS*/

static int mp2650_2_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;
	return 0;
}

static int mp2650_2_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;
	return 0;
}

#define mp2650_2_DUMP_REG_COUNT   0x15
static int mp2650_2_reg_2_dump_internal(void)
{
	int rc = 0;
	int addr;
	struct chip_mp2650 *chip = charger_ic_2;
	int val_buf[mp2650_2_DUMP_REG_COUNT + 2] = { 0x0 };

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	for (addr = mp2650_2_FIRST_REG; addr < mp2650_2_DUMP_REG_COUNT; addr++) {
		rc = mp2650_2_read_reg(addr, &val_buf[addr]);
		if (rc)
			chg_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
	}
	rc = mp2650_2_read_reg(0x48, &val_buf[mp2650_2_DUMP_REG_COUNT]);
	if (rc)
		chg_err("Couldn't read 0x48 rc = %d\n", rc);

	printk(KERN_INFO "mp2650_2_dump_reg: [%02x, %02x, %02x, %02x], [%02x, %02x, %02x, %02x], "
			"[%02x, %02x, %02x, %02x], [%02x, %02x, %02x, %02x], "
			"[%02x, %02x, %02x, %02x], reg[0x14]=%02x,reg[0x48]=%02x \n",
			val_buf[0], val_buf[1], val_buf[2], val_buf[3],
			val_buf[4], val_buf[5], val_buf[6], val_buf[7],
			val_buf[8], val_buf[9], val_buf[10], val_buf[11],
			val_buf[12], val_buf[13], val_buf[14], val_buf[15],
			val_buf[16], val_buf[17], val_buf[18], val_buf[19],
			val_buf[20], val_buf[21]);

	memset(buck_ic_reg_info_2, 0, BATTERY_LOG_REG_MAX_SIZE);
	snprintf(buck_ic_reg_info_2, BATTERY_LOG_REG_MAX_SIZE,
		",0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,"
		"0x%02x,0x%02x,0x%02x,0x%02x",
		val_buf[0], val_buf[1], val_buf[2], val_buf[4], val_buf[8],
		val_buf[9], val_buf[19], val_buf[20], val_buf[21]);

	return 0;
}

static int mp2650_2_reg_2_dump(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = mp2650_2_reg_2_dump_internal();
	return rc;
}

static int buck_ic_dump_log_data(char *buffer, int size, void *dev_data)
{
	struct chip_mp2650 *chip = dev_data;

	if (!buffer || !chip)
		return -ENOMEM;

	strncpy(buffer, buck_ic_reg_info_2, sizeof(buck_ic_reg_info_2));

	return 0;
}

static int buck_ic_get_log_head(char *buffer, int size, void *dev_data)
{
	struct chip_mp2650 *chip = dev_data;

	if (!buffer || !chip)
		return -ENOMEM;

	snprintf(buffer, size,
		",in_curr[0x00],in_volt[0x01],chg_curr[0x02],chg_full[0x04],chg&otg_enable[0x08],"
		"chg_terminal_enable[0x09],status_reg[0x13],fault_reg[0x14],Hiz_enable[0x48]");

	return 0;
}

static struct battery_log_ops battlog_buck_ic_ops = {
	.dev_name = "buck_ic",
	.dump_log_head = buck_ic_get_log_head,
	.dump_log_content = buck_ic_dump_log_data,
};

static int mp2650_2_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int mp2650_2_input_present(struct oplus_chg_ic_dev *ic_dev, bool *present)
{
	struct chip_mp2650 *chg_ic;
	int pwr_good = 0;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chg_ic = oplus_chg_ic_get_drvdata(ic_dev);

	if (atomic_read(&chg_ic->charger_suspended) == 1) {
		*present = chg_ic->acok;
		return 0;
	}

	rc = mp2650_2_read_reg(REG13_mp2650_2_ADDRESS, &pwr_good);
	if (rc < 0) {
		chg_err("can't read power good status, rc=%d\n", rc);
		pwr_good = REG13_mp2650_2_VIN_POWER_GOOD_YES;
	}
	pwr_good = pwr_good & REG13_mp2650_2_VIN_POWER_GOOD_MASK;

	*present = chg_ic->acok && (!!pwr_good);

	return 0;
}

static int mp2650_2_input_suspend(struct oplus_chg_ic_dev *ic_dev, bool suspend)
{
	struct chip_mp2650 *chg_ic;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chg_info("input suspend = %d\n", suspend);
	chg_ic = oplus_chg_ic_get_drvdata(ic_dev);

	if (atomic_read(&chg_ic->charger_suspended) == 1) {
		return 0;
	}

	rc = mp2650_2_config_interface(REG08_mp2650_2_ADDRESS,
				     suspend ? REG08_mp2650_2_LEARN_EN_ENABLE :
						     REG08_mp2650_2_LEARN_EN_DISABLE,
				     REG08_mp2650_2_LEARN_EN_MASK);
	if (rc < 0)
		chg_err("can't suspend charger, rc = %d\n", rc);

	return rc;
}

static int mp2650_2_input_is_suspend(struct oplus_chg_ic_dev *ic_dev,
				   bool *suspend)
{
	struct chip_mp2650 *chg_ic;
	int data = 0;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chg_ic = oplus_chg_ic_get_drvdata(ic_dev);
	if (atomic_read(&chg_ic->charger_suspended) == 1) {
		*suspend = false;
		return 0;
	}

	rc = mp2650_2_read_reg(REG08_mp2650_2_ADDRESS, &data);
	if (rc) {
		chg_err("can't read REG1C1D_mp2650_2_ADDRESS, rc = %d\n", rc);
		return rc;
	}

	if (data & REG08_mp2650_2_LEARN_EN_MASK) {
		*suspend = true;
	} else {
		*suspend = false;
	}

	return rc;
}

static int mp2650_2_output_suspend(struct oplus_chg_ic_dev *ic_dev, bool suspend)
{
	int rc = 0;

	rc = suspend ? mp2650_2_disable_charging_2() : mp2650_2_enable_charging_2();

	return rc;
}

static int mp2650_2_output_is_suspend(struct oplus_chg_ic_dev *ic_dev,
				    bool *suspend)
{
	*suspend = mp2650_2_check_charging_enable_2();

	return 0;
}

static int mp2650_2_set_icl(struct oplus_chg_ic_dev *ic_dev,
			  bool vooc_mode, bool step, int icl_ma)
{
	struct chip_mp2650 *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (vooc_mode && icl_ma > 0) {
		rc = mp2650_2_input_current_limit_ctrl_by_vooc_write_2(icl_ma);
		return rc;
	}

	mp2650_2_set_charger_vsys_threshold(chip, MP2762_VSYS_THR_104);
	mp2650_2_burst_mode_enable(false);

	if (step)
		rc = mp2650_2_input_current_limit_write(chip, icl_ma);
	else
		rc = mp2650_2_input_current_limit_without_aicl(chip, icl_ma);

	mp2650_2_burst_mode_enable(true);
	mp2650_2_set_charger_vsys_threshold(chip, MP2762_VSYS_THR_101);

	return rc;
}

static int mp2650_2_get_icl(struct oplus_chg_ic_dev *ic_dev, int *icl_ma)
{
	*icl_ma = 3000;

	return 0;
}

static int mp2650_2_set_fcc(struct oplus_chg_ic_dev *ic_dev, int fcc_ma)
{
	int rc = 0;
	rc = mp2650_2_charging_current_write_fast_2(fcc_ma);

	return rc;
}

static int mp2650_2_set_fv(struct oplus_chg_ic_dev *ic_dev, int fv_mv)
{
	int rc = 0;
	rc = mp2650_2_float_voltage_write_2(fv_mv);

	return rc;
}

static int mp2650_2_set_iterm(struct oplus_chg_ic_dev *ic_dev, int iterm_ma)
{
	int rc = 0;
	rc = mp2650_2_set_termchg_current_2(iterm_ma);

	return rc;
}

static int mp2650_2_set_rechg_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	int rc = 0;
	rc = mp2650_2_set_rechg_voltage_2(vol_mv);

	return rc;
}

static int mp2650_2_get_input_curr(struct oplus_chg_ic_dev *ic_dev, int *curr_ma)
{
	*curr_ma = mp2650_2_get_ibus_current_2() / 1000;

	return 0;
}

static int mp2650_2_get_input_vol(struct oplus_chg_ic_dev *ic_dev, int *vol_mv)
{
	*vol_mv = mp2650_2_get_vbus_voltage_2();

	return 0;
}

static int mp2650_2_otg_boost_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc;

	if (en)
		rc = mp2650_2_otg_enable();
	else
		rc = mp2650_2_otg_disable_2();
	if (rc < 0)
		chg_err("can't %s otg boost, rc=%d\n",
			en ? "enable" : "disable", rc);

	return rc;
}

static int mp2650_2_curr_drop(struct oplus_chg_ic_dev *ic_dev)
{
	int i, rc;
	int pre_icl_index = 0;
	struct chip_mp2650 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	pre_icl_index = mp2650_2_get_pre_icl_index_2();
	if (pre_icl_index < 2) {
		/* <1.2A, have been set 500 in bqmp2650_2_hardware_init_2 */
	} else {
		for (i = pre_icl_index - 1; i > 0; i--) {
			switch(i) {
			case 1:
				rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS, REG00_mp2650_2_1ST_CURRENT_LIMIT_900MA, REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS, REG00_mp2650_2_1ST_CURRENT_LIMIT_900MA, REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case 2:
				rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS, REG00_mp2650_2_1ST_CURRENT_LIMIT_1100MA, REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS, REG00_mp2650_2_1ST_CURRENT_LIMIT_1100MA, REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case 3:
				rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS, REG00_mp2650_2_1ST_CURRENT_LIMIT_1400MA, REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS, REG00_mp2650_2_1ST_CURRENT_LIMIT_1400MA, REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case 4:
				rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS, REG00_mp2650_2_1ST_CURRENT_LIMIT_1600MA, REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS, REG00_mp2650_2_1ST_CURRENT_LIMIT_1600MA, REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			case 5:
				rc = mp2650_2_config_interface(REG00_mp2650_2_ADDRESS, REG00_mp2650_2_1ST_CURRENT_LIMIT_1900MA, REG00_mp2650_2_1ST_CURRENT_LIMIT_MASK);
				rc = mp2650_2_config_interface(REG0F_mp2650_2_ADDRESS, REG00_mp2650_2_1ST_CURRENT_LIMIT_1900MA, REG0F_mp2650_2_2ND_CURRENT_LIMIT_MASK);
				break;
			default:
				break;
			}
			msleep(50);
		}
	}

	return 0;
}

static int mp2650_2_wdt_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	struct chip_mp2650 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	if (enable)
		mp2650_2_set_wdt_timer_2(REG09_mp2650_2_WTD_TIMER_40S);
	else
		mp2650_2_set_wdt_timer_2(REG09_mp2650_2_WTD_TIMER_DISABLE);

	chg_info("mp2650_2_wdt_enable[%d]\n", enable);

	return 0;
}

static int mp2650_2_set_hardware_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct chip_mp2650 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	mp2650_2_hardware_init_2();

	return 0;
}


static void *oplus_chg_get_func(struct oplus_chg_ic_dev *ic_dev,
				enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	    }

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, mp2650_2_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, mp2650_2_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       mp2650_2_reg_2_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
					       mp2650_2_smt_test);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_PRESENT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_PRESENT,
					       mp2650_2_input_present);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND,
					       mp2650_2_input_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND,
			mp2650_2_input_is_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND,
			mp2650_2_output_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND,
			mp2650_2_output_is_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_ICL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_ICL,
					       mp2650_2_set_icl);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_ICL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_ICL,
					       mp2650_2_get_icl);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_FCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_FCC,
					       mp2650_2_set_fcc);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_FV:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_FV,
					       mp2650_2_set_fv);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_ITERM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_ITERM,
					       mp2650_2_set_iterm);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL,
					       mp2650_2_set_rechg_vol);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR,
			mp2650_2_get_input_curr);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL,
					       mp2650_2_get_input_vol);
		break;
	case OPLUS_IC_FUNC_OTG_BOOST_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_OTG_BOOST_ENABLE,
					       mp2650_2_otg_boost_enable);
		break;
	case OPLUS_IC_FUNC_BUCK_CURR_DROP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_CURR_DROP,
					       mp2650_2_curr_drop);
		break;
	case OPLUS_IC_FUNC_BUCK_WDT_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_WDT_ENABLE,
					       mp2650_2_wdt_enable);
		break;
	case OPLUS_IC_FUNC_BUCK_KICK_WDT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_KICK_WDT,
					       mp2650_2_kick_wdt_2);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_AICL_POINT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_AICL_POINT,
					       mp2650_2_set_aicl_point_2);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_VINDPM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_VINDPM,
					       mp2650_2_mms_set_vindpm_vol);
		break;
	case OPLUS_IC_FUNC_BUCK_HARDWARE_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_HARDWARE_INIT,
					       mp2650_2_set_hardware_init);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq mp2650_2_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_PLUGIN },
};

static int mp2650_driver_2_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	int ret = 0;
	struct chip_mp2650 *chg_ic;
	struct device_node *node = client->dev.of_node;
	enum oplus_chg_ic_type ic_type;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	int ic_index;

	chg_ic = devm_kzalloc(&client->dev, sizeof(struct chip_mp2650),
			      GFP_KERNEL);
	if (!chg_ic) {
		chg_err(" kzalloc() failed\n");
		return -ENOMEM;
	}

	chg_debug(" call \n");
	chg_ic->client = client;
	chg_ic->dev = &client->dev;
	i2c_set_clientdata(client, chg_ic);

	init_completion(&chg_ic->resume_ack);
	complete_all(&chg_ic->resume_ack);
	INIT_DELAYED_WORK(&chg_ic->plugin_work, mp2650_2_plugin_work);

	charger_ic_2 = chg_ic;
	atomic_set(&chg_ic->charger_suspended, 0);

	mp2650_2_dump_registers_2();
	mp2650_2_vbus_avoid_electric_config();
	chg_ic->probe_flag = true;
	mp2650_2_hardware_init_2();
	mp2650_2_gpio_init(chg_ic);

	register_charger_devinfo();
	ret = sysfs_create_group(&chg_ic->dev->kobj, &mp2650_2_attribute_group);
	if (ret < 0) {
		chg_debug(" sysfs_create_group error fail\n");
		/* /return ret; */
	}

#ifdef DEBUG_BY_FILE_OPS
	init_mp2650_2_write_log();
	init_mp2650_2_read_log();
#endif

	ret = of_property_read_u32(node, "oplus,ic_type", &ic_type);
	if (ret < 0) {
		chg_err("can't get ic type, rc=%d\n", ret);
		goto reg_ic_err;
	}
	ret = of_property_read_u32(node, "oplus,ic_index", &ic_index);
	if (ret < 0) {
		chg_err("can't get ic index, rc=%d\n", ret);
		goto reg_ic_err;
	}
	battlog_buck_ic_ops.dev_data = (void *)chg_ic;
	battery_log_ops_register_2(&battlog_buck_ic_ops);
	ic_cfg.name = node->name;
	ic_cfg.index = ic_index;
	snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "buck-mp2762");
	snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_get_func;
	ic_cfg.virq_data = mp2650_2_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(mp2650_2_virq_table);
	ic_cfg.of_node = node;
	chg_ic->ic_dev = devm_oplus_chg_ic_register_2(chg_ic->dev, &ic_cfg);
	if (!chg_ic->ic_dev) {
		ret = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto reg_ic_err;
	}
	chg_info("register %s\n", node->name);

	schedule_delayed_work(&chg_ic->plugin_work, 0);

	chg_info(" success\n");

	return ret;

reg_ic_err:
	chg_err("probe error\n");
	return ret;
}

static struct i2c_driver mp2650_2_i2c_driver;

static int mp2650_driver_2_remove(struct i2c_client *client)
{
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int mp2650_2_pm_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct chip_mp2650 *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	atomic_set(&chip->charger_suspended, 0);
	complete_all(&chip->resume_ack);
	return 0;
}

static int mp2650_2_pm_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct chip_mp2650 *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	reinit_completion(&chip->resume_ack);
	atomic_set(&chip->charger_suspended, 1);
	return 0;
}

static const struct dev_pm_ops mp2650_2_pm_ops = {
	.resume = mp2650_2_pm_resume,
	.suspend = mp2650_2_pm_suspend,
};
#else
static int mp2650_2_resume(struct i2c_client *client)
{
	struct chip_mp2650 *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	atomic_set(&chip->charger_suspended, 0);
	complete_all(&chip->resume_ack);
	return 0;
}

static int mp2650_2_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct chip_mp2650 *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	reinit_completion(&chip->resume_ack);
	atomic_set(&chip->charger_suspended, 1);
	return 0;
}
#endif

#define VBUS_VOLT_VALID_THRESHOLD	2000
static void mp2650_2_shutdown(struct i2c_client *client)
{
	mp2650_2_otg_disable_2();
	chg_info("shutdown\n");
}

static const struct of_device_id mp2650_2_match[] = {
	{ .compatible = "oplus,mp2650-charger" },
	{},
};

static const struct i2c_device_id mp2650_2_id[] = {
	{ "mp2650-charger", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, mp2650_2_id);

static struct i2c_driver mp2650_2_i2c_driver = {
	.driver		= {
		.name = "mp2650-charger",
		.owner	= THIS_MODULE,
		.of_match_table = mp2650_2_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm 	= &mp2650_2_pm_ops,
#endif
	},
	.probe		= mp2650_driver_2_probe,
	.remove		= mp2650_driver_2_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume		= mp2650_2_resume,
	.suspend	= mp2650_2_suspend,
#endif
	.shutdown	= mp2650_2_shutdown,
	.id_table	= mp2650_2_id,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(mp2650_2_i2c_driver);
#else
int mp2650_driver_2_init(void)
{
	int ret = 0;

	chg_debug(" start\n");

	if (i2c_add_driver(&mp2650_2_i2c_driver) != 0) {
		chg_err(" failed to register mp2650 i2c driver.\n");
	} else {
		chg_debug(" Success to register mp2650 i2c driver.\n");
	}

	return ret;
}

void mp2650_driver_2_exit(void)
{
	i2c_del_driver(&mp2650_2_i2c_driver);
}
oplus_chg_module_register(mp2650_driver_2);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/
MODULE_DESCRIPTION("Driver for mp2650 charger chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:mp2650-charger");
