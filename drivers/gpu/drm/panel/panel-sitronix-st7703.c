// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for panels based on Sitronix ST7703 controller, souch as:
 *
 * - Rocktech jh057n00900 5.5" MIPI-DSI panel
 *
 * Copyright (C) Purism SPC 2019
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <video/display_timing.h>
#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DRV_NAME "panel-sitronix-st7703"

/* Manufacturer specific Commands send via DSI */
#define ST7703_CMD_ALL_PIXEL_ON	 0x23

struct st7703 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *vcc;
	struct regulator *iovcc;
	bool prepared;

	struct dentry *debugfs;
	const struct st7703_panel_desc *desc;
};

struct st7703_panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	int (*init_sequence)(struct st7703 *ctx);
};

static inline struct st7703 *panel_to_st7703(struct drm_panel *panel)
{
	return container_of(panel, struct st7703, panel);
}

#define dsi_generic_write_seq(dsi, seq...) do {				\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_generic_write(dsi, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)

#define JD9365DA_DCS_SWITCH_PAGE	0xe0

#define jd9365da_switch_page(dsi, page) \
	dsi_generic_write_seq(dsi, JD9365DA_DCS_SWITCH_PAGE, (page))

#define DCS_GET_ID1            0xda
#define DCS_GET_ID2            0xdb
#define DCS_GET_ID3            0xdc

#define JD9365_RDDIDIF  0x04 /* Return ID1, ID2 and ID3. */

static int dsi_get_id(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct st7703 *ctx = panel_to_st7703(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 id[3];
	int ret;

	memset(id, 0x00, sizeof(id));

	ret = mipi_dsi_dcs_read(dsi, JD9365_RDDIDIF, id, 3);
	if (ret)
		dev_err(dev, "%s(): error reading RDDIDIF register\n", __func__);

	ret = mipi_dsi_dcs_read(dsi, DCS_GET_ID1, &id[0], 1);
	if (ret) {
		dev_err(dev, "%s(): error reading ID1 register\n", __func__);
		return ret;
	}

	dev_warn(dev, "%s(): ID1 = $%02X\n", __func__, id[0]);

	ret = mipi_dsi_dcs_read(dsi, DCS_GET_ID2, &id[1], 1);
	if (ret) {
		dev_err(dev, "%s(): error reading ID2 register\n", __func__);
		return ret;
	}

	dev_warn(dev, "%s(): ID2 = $%02X\n", __func__, id[1]);

	ret = mipi_dsi_dcs_read(dsi, DCS_GET_ID3, &id[2], 1);
	if (ret) {
		dev_err(dev, "%s(): error reading ID3 register\n", __func__);
		return ret;
	}

	dev_warn(dev, "%s(): ID3 = $%02X\n", __func__, id[2]);

	return ret;
}

static int jadard_enable_standard_cmds(struct mipi_dsi_device *dsi)
{
	dsi_generic_write_seq(dsi, 0xe1, 0x93);
	dsi_generic_write_seq(dsi, 0xe2, 0x65);
	dsi_generic_write_seq(dsi, 0xe3, 0xf8);
	dsi_generic_write_seq(dsi, 0x80, 0x03);

	return 0;
}

static int jh057n_init_sequence(struct st7703 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	dev_warn(ctx->dev, "%s() entry\n", __func__);

	jd9365da_switch_page(dsi, 0x00);
	jadard_enable_standard_cmds(dsi);

	jd9365da_switch_page(dsi, 0x01);
	dsi_generic_write_seq(dsi, 0x00, 0x00);
	dsi_generic_write_seq(dsi, 0x01, 0x2B);
	dsi_generic_write_seq(dsi, 0x03, 0x10);
	dsi_generic_write_seq(dsi, 0x04, 0x2B);
	dsi_generic_write_seq(dsi, 0x0C, 0x74);
	dsi_generic_write_seq(dsi, 0x17, 0x00);
	dsi_generic_write_seq(dsi, 0x18, 0xCF);
	dsi_generic_write_seq(dsi, 0x19, 0x01);
	dsi_generic_write_seq(dsi, 0x1A, 0x00);
	dsi_generic_write_seq(dsi, 0x1B, 0xCF);
	dsi_generic_write_seq(dsi, 0x1C, 0x01);
	dsi_generic_write_seq(dsi, 0x24, 0xFE);
	dsi_generic_write_seq(dsi, 0x37, 0x09);
	dsi_generic_write_seq(dsi, 0x38, 0x04);
	dsi_generic_write_seq(dsi, 0x39, 0x00);
	dsi_generic_write_seq(dsi, 0x3A, 0x01);
	dsi_generic_write_seq(dsi, 0x3C, 0x5C);
	dsi_generic_write_seq(dsi, 0x3D, 0xFF);
	dsi_generic_write_seq(dsi, 0x3E, 0xFF);
	dsi_generic_write_seq(dsi, 0x3F, 0x7F);
	dsi_generic_write_seq(dsi, 0x40, 0x02);
	dsi_generic_write_seq(dsi, 0x41, 0xC8);
	dsi_generic_write_seq(dsi, 0x42, 0x66);
	dsi_generic_write_seq(dsi, 0x43, 0x10);
	dsi_generic_write_seq(dsi, 0x44, 0x0F);
	dsi_generic_write_seq(dsi, 0x45, 0x28);
	dsi_generic_write_seq(dsi, 0x55, 0x02);
	dsi_generic_write_seq(dsi, 0x57, 0x8D);
	dsi_generic_write_seq(dsi, 0x59, 0x0A);
	dsi_generic_write_seq(dsi, 0x5A, 0x29);
	dsi_generic_write_seq(dsi, 0x5B, 0x1A);
	dsi_generic_write_seq(dsi, 0x5D, 0x7F);
	dsi_generic_write_seq(dsi, 0x5E, 0x65);
	dsi_generic_write_seq(dsi, 0x5F, 0x53);
	dsi_generic_write_seq(dsi, 0x60, 0x45);
	dsi_generic_write_seq(dsi, 0x61, 0x3E);
	dsi_generic_write_seq(dsi, 0x62, 0x30);
	dsi_generic_write_seq(dsi, 0x63, 0x33);
	dsi_generic_write_seq(dsi, 0x64, 0x1F);
	dsi_generic_write_seq(dsi, 0x65, 0x3B);
	dsi_generic_write_seq(dsi, 0x66, 0x3C);
	dsi_generic_write_seq(dsi, 0x67, 0x3F);
	dsi_generic_write_seq(dsi, 0x68, 0x5D);
	dsi_generic_write_seq(dsi, 0x69, 0x49);
	dsi_generic_write_seq(dsi, 0x6A, 0x4D);
	dsi_generic_write_seq(dsi, 0x6B, 0x3D);
	dsi_generic_write_seq(dsi, 0x6C, 0x37);
	dsi_generic_write_seq(dsi, 0x6D, 0x28);
	dsi_generic_write_seq(dsi, 0x6E, 0x15);
	dsi_generic_write_seq(dsi, 0x6F, 0x00);
	dsi_generic_write_seq(dsi, 0x70, 0x7F);
	dsi_generic_write_seq(dsi, 0x71, 0x65);
	dsi_generic_write_seq(dsi, 0x72, 0x53);
	dsi_generic_write_seq(dsi, 0x73, 0x45);
	dsi_generic_write_seq(dsi, 0x74, 0x3E);
	dsi_generic_write_seq(dsi, 0x75, 0x30);
	dsi_generic_write_seq(dsi, 0x76, 0x33);
	dsi_generic_write_seq(dsi, 0x77, 0x1F);
	dsi_generic_write_seq(dsi, 0x78, 0x3B);
	dsi_generic_write_seq(dsi, 0x79, 0x3C);
	dsi_generic_write_seq(dsi, 0x7A, 0x3F);
	dsi_generic_write_seq(dsi, 0x7B, 0x5D);
	dsi_generic_write_seq(dsi, 0x7C, 0x49);
	dsi_generic_write_seq(dsi, 0x7D, 0x4D);
	dsi_generic_write_seq(dsi, 0x7E, 0x3D);
	dsi_generic_write_seq(dsi, 0x7F, 0x44);
	dsi_generic_write_seq(dsi, 0x80, 0x28);
	dsi_generic_write_seq(dsi, 0x81, 0x15);
	dsi_generic_write_seq(dsi, 0x82, 0x00);

	jd9365da_switch_page(dsi, 0x02);
	dsi_generic_write_seq(dsi, 0x00, 0x41);
	dsi_generic_write_seq(dsi, 0x01, 0x5F);
	dsi_generic_write_seq(dsi, 0x02, 0x5F);
	dsi_generic_write_seq(dsi, 0x03, 0x4B);
	dsi_generic_write_seq(dsi, 0x04, 0x5F);
	dsi_generic_write_seq(dsi, 0x05, 0x5C);
	dsi_generic_write_seq(dsi, 0x06, 0x5F);
	dsi_generic_write_seq(dsi, 0x07, 0x49);
	dsi_generic_write_seq(dsi, 0x08, 0x5F);
	dsi_generic_write_seq(dsi, 0x09, 0x5A);
	dsi_generic_write_seq(dsi, 0x0A, 0x5F);
	dsi_generic_write_seq(dsi, 0x0B, 0x47);
	dsi_generic_write_seq(dsi, 0x0C, 0x5F);
	dsi_generic_write_seq(dsi, 0x0D, 0x4F);
	dsi_generic_write_seq(dsi, 0x0E, 0x5F);
	dsi_generic_write_seq(dsi, 0x0F, 0x45);
	dsi_generic_write_seq(dsi, 0x10, 0x5F);
	dsi_generic_write_seq(dsi, 0x11, 0x4D);
	dsi_generic_write_seq(dsi, 0x12, 0x5F);
	dsi_generic_write_seq(dsi, 0x13, 0x5E);
	dsi_generic_write_seq(dsi, 0x14, 0x51);
	dsi_generic_write_seq(dsi, 0x15, 0x5F);
	dsi_generic_write_seq(dsi, 0x16, 0x40);
	dsi_generic_write_seq(dsi, 0x17, 0x5F);
	dsi_generic_write_seq(dsi, 0x18, 0x5F);
	dsi_generic_write_seq(dsi, 0x19, 0x4A);
	dsi_generic_write_seq(dsi, 0x1A, 0x5F);
	dsi_generic_write_seq(dsi, 0x1B, 0x5B);
	dsi_generic_write_seq(dsi, 0x1C, 0x5F);
	dsi_generic_write_seq(dsi, 0x1D, 0x48);
	dsi_generic_write_seq(dsi, 0x1E, 0x5F);
	dsi_generic_write_seq(dsi, 0x1F, 0x59);
	dsi_generic_write_seq(dsi, 0x20, 0x5F);
	dsi_generic_write_seq(dsi, 0x21, 0x46);
	dsi_generic_write_seq(dsi, 0x22, 0x5F);
	dsi_generic_write_seq(dsi, 0x23, 0x4E);
	dsi_generic_write_seq(dsi, 0x24, 0x5F);
	dsi_generic_write_seq(dsi, 0x25, 0x44);
	dsi_generic_write_seq(dsi, 0x26, 0x5F);
	dsi_generic_write_seq(dsi, 0x27, 0x4C);
	dsi_generic_write_seq(dsi, 0x28, 0x5F);
	dsi_generic_write_seq(dsi, 0x29, 0x5E);
	dsi_generic_write_seq(dsi, 0x2A, 0x50);
	dsi_generic_write_seq(dsi, 0x2B, 0x5F);
	dsi_generic_write_seq(dsi, 0x2C, 0x10);
	dsi_generic_write_seq(dsi, 0x2D, 0x1F);
	dsi_generic_write_seq(dsi, 0x2E, 0x1F);
	dsi_generic_write_seq(dsi, 0x2F, 0x0C);
	dsi_generic_write_seq(dsi, 0x30, 0x1F);
	dsi_generic_write_seq(dsi, 0x31, 0x04);
	dsi_generic_write_seq(dsi, 0x32, 0x1F);
	dsi_generic_write_seq(dsi, 0x33, 0x0E);
	dsi_generic_write_seq(dsi, 0x34, 0x1F);
	dsi_generic_write_seq(dsi, 0x35, 0x06);
	dsi_generic_write_seq(dsi, 0x36, 0x1F);
	dsi_generic_write_seq(dsi, 0x37, 0x19);
	dsi_generic_write_seq(dsi, 0x38, 0x1F);
	dsi_generic_write_seq(dsi, 0x39, 0x08);
	dsi_generic_write_seq(dsi, 0x3A, 0x1F);
	dsi_generic_write_seq(dsi, 0x3B, 0x1B);
	dsi_generic_write_seq(dsi, 0x3C, 0x1F);
	dsi_generic_write_seq(dsi, 0x3D, 0x0A);
	dsi_generic_write_seq(dsi, 0x3E, 0x1E);
	dsi_generic_write_seq(dsi, 0x3F, 0x1F);
	dsi_generic_write_seq(dsi, 0x40, 0x00);
	dsi_generic_write_seq(dsi, 0x41, 0x1F);
	dsi_generic_write_seq(dsi, 0x42, 0x11);
	dsi_generic_write_seq(dsi, 0x43, 0x1F);
	dsi_generic_write_seq(dsi, 0x44, 0x1F);
	dsi_generic_write_seq(dsi, 0x45, 0x0D);
	dsi_generic_write_seq(dsi, 0x46, 0x1F);
	dsi_generic_write_seq(dsi, 0x47, 0x05);
	dsi_generic_write_seq(dsi, 0x48, 0x1F);
	dsi_generic_write_seq(dsi, 0x49, 0x0F);
	dsi_generic_write_seq(dsi, 0x4A, 0x1F);
	dsi_generic_write_seq(dsi, 0x4B, 0x07);
	dsi_generic_write_seq(dsi, 0x4C, 0x1F);
	dsi_generic_write_seq(dsi, 0x4D, 0x1A);
	dsi_generic_write_seq(dsi, 0x4E, 0x1F);
	dsi_generic_write_seq(dsi, 0x4F, 0x09);
	dsi_generic_write_seq(dsi, 0x50, 0x1F);
	dsi_generic_write_seq(dsi, 0x51, 0x1C);
	dsi_generic_write_seq(dsi, 0x52, 0x1F);
	dsi_generic_write_seq(dsi, 0x53, 0x0B);
	dsi_generic_write_seq(dsi, 0x54, 0x1E);
	dsi_generic_write_seq(dsi, 0x55, 0x1F);
	dsi_generic_write_seq(dsi, 0x56, 0x01);
	dsi_generic_write_seq(dsi, 0x57, 0x1F);
	dsi_generic_write_seq(dsi, 0x58, 0x40);
	dsi_generic_write_seq(dsi, 0x5B, 0x10);
	dsi_generic_write_seq(dsi, 0x5C, 0x01);
	dsi_generic_write_seq(dsi, 0x5D, 0x70);
	dsi_generic_write_seq(dsi, 0x5E, 0x01);
	dsi_generic_write_seq(dsi, 0x5F, 0x02);
	dsi_generic_write_seq(dsi, 0x60, 0x70);
	dsi_generic_write_seq(dsi, 0x61, 0x01);
	dsi_generic_write_seq(dsi, 0x62, 0x02);
	dsi_generic_write_seq(dsi, 0x63, 0x06);
	dsi_generic_write_seq(dsi, 0x64, 0x4A);
	dsi_generic_write_seq(dsi, 0x65, 0x56);
	dsi_generic_write_seq(dsi, 0x66, 0x4F);
	dsi_generic_write_seq(dsi, 0x67, 0xF7);
	dsi_generic_write_seq(dsi, 0x68, 0x01);
	dsi_generic_write_seq(dsi, 0x69, 0x06);
	dsi_generic_write_seq(dsi, 0x6A, 0x4A);
	dsi_generic_write_seq(dsi, 0x6B, 0x10);
	dsi_generic_write_seq(dsi, 0x6C, 0x00);
	dsi_generic_write_seq(dsi, 0x6D, 0x00);
	dsi_generic_write_seq(dsi, 0x6E, 0x00);
	dsi_generic_write_seq(dsi, 0x6F, 0x88);

	jd9365da_switch_page(dsi, 0x04);
	dsi_generic_write_seq(dsi, 0x00, 0x0E);
	dsi_generic_write_seq(dsi, 0x02, 0xB3);
	dsi_generic_write_seq(dsi, 0x09, 0x60);
	dsi_generic_write_seq(dsi, 0x0e, 0x4A);
	dsi_generic_write_seq(dsi, 0x37, 0x58);
	dsi_generic_write_seq(dsi, 0x2b, 0x0F);

	jd9365da_switch_page(dsi, 0x05);
	dsi_generic_write_seq(dsi, 0x15, 0x34);
	dsi_generic_write_seq(dsi, 0x16, 0x76);

	jd9365da_switch_page(dsi, 0x00);

	msleep(120);

	mipi_dsi_dcs_exit_sleep_mode(dsi);

	msleep(120);

	mipi_dsi_dcs_set_display_on(dsi);

	msleep(20);

	mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	(void) dsi_get_id(&ctx->panel);

	dev_warn(ctx->dev, "%s() exit\n", __func__);

	return 0;
}

#define X_RES 600
#define Y_RES 1600

static const struct drm_display_mode jh057n00900_mode = {
	.clock		= (X_RES + 20 + 20 + 20) * (Y_RES + 20 + 4 + 20) * 60 / 1000,

	.hdisplay	= X_RES,
	.hsync_start	= X_RES + 20,
	.hsync_end	= X_RES + 20 + 20,
	.htotal		= X_RES + 20 + 20 + 20,

	.vdisplay	= Y_RES,
	.vsync_start	= Y_RES + 20,
	.vsync_end	= Y_RES + 20 + 4,
	.vtotal		= Y_RES + 20 + 4 + 20,

	.width_mm	= 83,
	.height_mm	= 221,
	.flags          = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

struct st7703_panel_desc jh057n00900_panel_desc = {
	.mode = &jh057n00900_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO |
		MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = jh057n_init_sequence,
};

#define dsi_dcs_write_seq(dsi, cmd, seq...) do {			\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_dcs_write(dsi, cmd, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)


static int st7703_enable(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = ctx->desc->init_sequence(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "Panel init sequence failed: %d\n", ret);
		return ret;
	}

	msleep(20);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	/* Panel is operational 120 msec after reset */
	msleep(60);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	dev_dbg(ctx->dev, "Panel init sequence done\n");

	return 0;
}

static int st7703_disable(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		dev_err(ctx->dev, "Failed to turn off the display: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		dev_err(ctx->dev, "Failed to enter sleep mode: %d\n", ret);

	return 0;
}

static int st7703_unprepare(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);

	if (!ctx->prepared)
		return 0;

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->iovcc);
	regulator_disable(ctx->vcc);
	ctx->prepared = false;

	return 0;
}

static int st7703_prepare(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	dev_dbg(ctx->dev, "Resetting the panel\n");
	ret = regulator_enable(ctx->vcc);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable vcc supply: %d\n", ret);
		return ret;
	}
	ret = regulator_enable(ctx->iovcc);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable iovcc supply: %d\n", ret);
		goto disable_vcc;
	}

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(5);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(10);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(130);

	ctx->prepared = true;

	return 0;

disable_vcc:
	regulator_disable(ctx->vcc);
	return ret;
}

static int st7703_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->mode);
	if (!mode) {
		dev_err(ctx->dev, "Failed to add mode %ux%u@%u\n",
			ctx->desc->mode->hdisplay, ctx->desc->mode->vdisplay,
			drm_mode_vrefresh(ctx->desc->mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs st7703_drm_funcs = {
	.disable   = st7703_disable,
	.unprepare = st7703_unprepare,
	.prepare   = st7703_prepare,
	.enable	   = st7703_enable,
	.get_modes = st7703_get_modes,
};

static int allpixelson_set(void *data, u64 val)
{
	struct st7703 *ctx = data;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	dev_dbg(ctx->dev, "Setting all pixels on\n");
	dsi_generic_write_seq(dsi, ST7703_CMD_ALL_PIXEL_ON);
	msleep(val * 1000);
	/* Reset the panel to get video back */
	drm_panel_disable(&ctx->panel);
	drm_panel_unprepare(&ctx->panel);
	drm_panel_prepare(&ctx->panel);
	drm_panel_enable(&ctx->panel);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(allpixelson_fops, NULL,
			allpixelson_set, "%llu\n");

static void st7703_debugfs_init(struct st7703 *ctx)
{
	ctx->debugfs = debugfs_create_dir(DRV_NAME, NULL);

	debugfs_create_file("allpixelson", 0600, ctx->debugfs, ctx,
			    &allpixelson_fops);
}

static void st7703_debugfs_remove(struct st7703 *ctx)
{
	debugfs_remove_recursive(ctx->debugfs);
	ctx->debugfs = NULL;
}

static int st7703_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct st7703 *ctx;
	int ret;

	dev_err(dev, "%s(): entry\n", __func__);

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset gpio\n");
		return PTR_ERR(ctx->reset_gpio);
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	ctx->desc = of_device_get_match_data(dev);

	dsi->mode_flags = ctx->desc->mode_flags;
	dsi->format = ctx->desc->format;
	dsi->lanes = ctx->desc->lanes;

	dev_warn(dev, "%s(): pixel clock: %d MHz\n", __func__, ctx->desc->mode->clock);
	dev_warn(dev, "%s(): mode_flags:  $%04X\n", __func__, (u16) dsi->mode_flags);

	ctx->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(ctx->vcc)) {
		ret = PTR_ERR(ctx->vcc);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request vcc regulator: %d\n", ret);
		return ret;
	}
	ctx->iovcc = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(ctx->iovcc)) {
		ret = PTR_ERR(ctx->iovcc);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request iovcc regulator: %d\n", ret);
		return ret;
	}

	drm_panel_init(&ctx->panel, dev, &st7703_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach failed (%d). Is host ready?\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_info(dev, "%ux%u@%u %ubpp dsi %udl - ready\n",
		 ctx->desc->mode->hdisplay, ctx->desc->mode->vdisplay,
		 drm_mode_vrefresh(ctx->desc->mode),
		 mipi_dsi_pixel_format_to_bpp(dsi->format), dsi->lanes);

	st7703_debugfs_init(ctx);
	return 0;
}

static void st7703_shutdown(struct mipi_dsi_device *dsi)
{
	struct st7703 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = drm_panel_unprepare(&ctx->panel);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to unprepare panel: %d\n", ret);

	ret = drm_panel_disable(&ctx->panel);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to disable panel: %d\n", ret);
}

static int st7703_remove(struct mipi_dsi_device *dsi)
{
	struct st7703 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	st7703_shutdown(dsi);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);

	st7703_debugfs_remove(ctx);

	return 0;
}

static const struct of_device_id st7703_of_match[] = {
	{ .compatible = "rocktech,jh057n00900", .data = &jh057n00900_panel_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, st7703_of_match);

static struct mipi_dsi_driver st7703_driver = {
	.probe	= st7703_probe,
	.remove = st7703_remove,
	.shutdown = st7703_shutdown,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = st7703_of_match,
	},
};
module_mipi_dsi_driver(st7703_driver);

MODULE_AUTHOR("Guido Günther <agx@sigxcpu.org>");
MODULE_DESCRIPTION("DRM driver for Sitronix ST7703 based MIPI DSI panels");
MODULE_LICENSE("GPL v2");
