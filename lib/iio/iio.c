/* SPDX-License-Identifier: GPL-2.0-only */

/* The industrial I/O core
 *
 * 
 */

#include <zephyr/iio.h>
#include <zephyr/iio-private.h>
#include <zephyr/iio-backend.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zephyr/posix/sys/utsname.h>

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/iterable_sections.h>


static const char * const device_attrs_denylist[] = {
	"dev",
	"uevent",
	"waiting_for_supplier",
};

char *iio_strdup(const char *str)
{
#if defined(_WIN32)
	return _strdup(str);
#elif defined(HAS_STRDUP)
	return strdup(str);
#else
	size_t len = strlen(str);
	char *buf = malloc(len + 1);

	if (buf)
		memcpy(buf, str, len + 1);
	return buf;
#endif
}

static struct iio_dev zephyr_iio_dev = {
    .modes = 1,
    .dev = NULL,
    .channels = NULL,
    .num_channels = 1,
    .name = "zephyr_iio_dev",
    .priv = NULL
};

struct iio_device * iio_context_add_device(struct iio_context *ctx,
					   const char *id, const char *name,
					   const char *label)
{
	struct iio_device *dev, **devs;
	char *new_id, *new_name = NULL, *new_label = NULL;

	dev = zalloc(sizeof(*dev));
	if (!dev)
		return NULL;

	new_id = iio_strdup(id);
	if (!new_id)
		goto err_free_dev;

	if (name) {
		new_name = iio_strdup(name);
		if (!new_name)
			goto err_free_id;
	}

	if (label) {
		new_label = iio_strdup(label);
		if (!new_label)
			goto err_free_name;
	}

	dev->id = new_id;
	dev->ctx = ctx;
	dev->name = new_name;
	dev->label = new_label;
	// zephyr_dev simulate 
	// STRUCT_SECTION_FOREACH(adxl345_dev_data, sensor) {
	// 	if (sensor->dev != NULL) {
	// 		zephyr_iio_dev.dev = sensor->dev;
	// 	}
	// }
	dev->zephyr_dev = &zephyr_iio_dev;

	devs = realloc(ctx->devices, (ctx->nb_devices + 1) * sizeof(*dev));
	if (!devs)
		goto err_free_label;

	devs[ctx->nb_devices++] = dev;
	ctx->devices = devs;

	//ctx_dbg(ctx, "Added device \'%s\' to context \'%s\'\n",
	//	dev->id, ctx->name);

	//iio_sort_devices(ctx);

	return dev;

err_free_label:
	free(new_label);
err_free_name:
	free(new_name);
err_free_id:
	free(new_id);
err_free_dev:
	free(dev);
	return NULL;
}

// This function reads the content of the "name" attribute for the IIO device with the given id and stores it in the name buffer.
// It is typically used to get the human-readable name of an IIO device from sysfs.
static ssize_t local_do_read_dev_attr(const char *id, unsigned int buf_id,
				      const char *attr, char *dst, size_t len,
				      enum iio_attr_type type)
{
	// FILE *f;
	// char buf[1024];
	// ssize_t ret;

	// if (buf_id > 0)
	// 	return -ENOSYS;

	// switch (type) {
	// 	case IIO_ATTR_TYPE_DEVICE:
	// 		snprintf(buf, sizeof(buf), "/sys/bus/iio/devices/%s/%s",
	// 						id, attr);
	// 		break;
	// 	default:
	// 		return -EINVAL;
	// }

	// f = fopen(buf, "re");
	// if (!f)
	// 	return -errno;

	// ret = fread(dst, 1, len, f);

	// /* if we didn't read the entire file, fail */
	// if (!feof(f))
	// 	ret = -EFBIG;

	// if (ret > 0)
	// 	dst[ret - 1] = '\0';
	// else
	// 	dst[0] = '\0';

	// fflush(f);
	// if (ferror(f))
	// 	ret = -errno;
	// fclose(f);
	// return ret;
	return 0;
}
static bool is_channel(const struct iio_device *dev, const char *attr, bool strict)
{
	char *ptr = NULL;

	if (!strncmp(attr, "in_timestamp_", sizeof("in_timestamp_") - 1))
		return true;
	if (!strncmp(attr, "in_", 3))
		ptr = strchr(attr + 3, '_');
	else if (!strncmp(attr, "out_", 4))
		ptr = strchr(attr + 4, '_');
	if (!ptr)
		return false;
	if (!strict)
		return true;
	if (*(ptr - 1) >= '0' && *(ptr - 1) <= '9')
		return true;

	// if (find_channel_modifier(ptr + 1, NULL) != IIO_NO_MOD)
	// 	return true;
	return false;
}

int iio_add_attr(union iio_pointer p, struct iio_attr_list *attrs,
		 const char *name, const char *filename,
		 enum iio_attr_type type)
{
	struct iio_attr *attr;

	attr = realloc(attrs->attrs, (1 + attrs->num) * sizeof(*attrs->attrs));
	if (!attr)
		return -ENOMEM;

	attrs->attrs = attr;
	attr[attrs->num] = (struct iio_attr){
		.iio = p,
		.type = type,
		.name = iio_strdup(name),
		.filename = NULL,
	};

	if (!attr[attrs->num].name)
		return -ENOMEM;

	if (filename) {
		attr[attrs->num].filename = iio_strdup(filename);

		if (!attr[attrs->num].filename) {
			free((char *) attr[attrs->num].name);
			return -ENOMEM;
		}
	} else {
		attr[attrs->num].filename = attr[attrs->num].name;
	}

	attrs->num++;

	//iio_sort_attrs(attrs);

	return 0;
}

static const char * const attr_type_string[] = {
	"",
	" debug",
	" buffer",
};

int iio_device_add_attr(struct iio_device *dev,
			const char *name, enum iio_attr_type type)
{
	union iio_pointer p = { .dev = dev, };
	int ret;

	ret = iio_add_attr(p, &dev->attrlist[type], name, NULL, type);
	if (ret < 0)
		return ret;

	//dev_dbg(dev, "Added%s attr \'%s\'\n", attr_type_string[type], name);
	return 0;
}

static int add_attr_to_device(struct iio_device *dev, const char *attr)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(device_attrs_denylist); i++)
		if (!strcmp(device_attrs_denylist[i], attr))
			return 0;

	if (!strcmp(attr, "name") || !strcmp(attr, "label"))
		return 0;

	return iio_device_add_attr(dev, attr, IIO_ATTR_TYPE_DEVICE);
}
bool iio_channel_is_output(const struct iio_channel *chn)
{
	return chn->is_output;
}

struct iio_channel * iio_device_find_channel(const struct iio_device *dev,
		const char *name, bool output)
{
	unsigned int i;
	for (i = 0; i < dev->nb_channels; i++) {
		struct iio_channel *chn = dev->channels[i];
		if (iio_channel_is_output(chn) != output)
			continue;

		if (!strcmp(chn->id, name) ||
				(chn->label && !strcmp(chn->label, name)) ||
				(chn->name && !strcmp(chn->name, name)))
			return chn;
	}
	return NULL;
}

static char * get_channel_id(struct iio_device *dev, const char *attr)
{
	char *res, *ptr = strchr(attr, '_');
	size_t len;

	//if (!WITH_HWMON || !iio_device_is_hwmon(dev)) {
		attr = ptr + 1;
		ptr = strchr(attr, '_');
		//if (find_channel_modifier(ptr + 1, &len) != IIO_NO_MOD)
			//ptr += len + 1;
	//} else if (!ptr) {
		/*
		 * Attribute is 'pwmX' without underscore: the attribute name
		 * is our channel ID.
		 */
		//return iio_strdup(attr);
	//}

	res = malloc(ptr - attr + 1);
	if (!res)
		return NULL;

	memcpy(res, attr, ptr - attr);
	res[ptr - attr] = 0;
	return res;
}
struct iio_channel_pdata {
	char *enable_fn;
	struct iio_attr_list protected;
};

static int add_attr_to_channel(struct iio_channel *chn,
			       const char *name, const char *path,
			       bool is_scan_element)
{
	struct iio_channel_pdata *pdata = chn->pdata;
	union iio_pointer p = { .chn = chn, };
	struct iio_attr_list *attrs;
	int ret;
	size_t lbl_len = strlen("_label");
	char label[512];
	const char *dev_id;

	if (strlen(name) >= lbl_len &&
		!strcmp(name + strlen(name) - lbl_len, "_label")) {
		//dev_id = iio_device_get_id(iio_channel_get_device(chn));
		ret = local_do_read_dev_attr(dev_id, 0, name,
			label, sizeof(label), IIO_ATTR_TYPE_CHANNEL);
		if (ret > 0)
			chn->label = iio_strdup(label);
		else
			//chn_perror(chn, ret, "Unable to read channel label");

		return 0;
	}

	//name = get_short_attr_name(chn, name);

	attrs = is_scan_element ? &pdata->protected : &chn->attrlist;
	ret = iio_add_attr(p, attrs, name, path, IIO_ATTR_TYPE_CHANNEL);
	if (ret)
		return ret;

	// chn_dbg(chn, "Added%s attr \'%s\' to channel \'%s\'\n",
	// 	is_scan_element ? " protected" : "", name, chn->id);
	return 0;
}

static int add_channel_to_device(struct iio_device *dev,
		struct iio_channel *chn)
{
	struct iio_channel **channels = realloc(dev->channels,
			(dev->nb_channels + 1) * sizeof(struct iio_channel *));
	if (!channels)
		return -ENOMEM;

	channels[dev->nb_channels++] = chn;
	dev->channels = channels;

	// dev_dbg(dev, "Added %s channel \'%s\' to device \'%s\'\n",
	// 	chn->is_output ? "output" : "input", chn->id, dev->id);

	return 0;
}
static struct iio_channel *create_channel(struct iio_device *dev,
		char *id, const char *attr, const char *path,
		bool is_scan_element)
{
	struct iio_channel *chn;
	int err = -ENOMEM;

	chn = zalloc(sizeof(*chn));
	if (!chn)
		return iio_ptr(-ENOMEM);

	chn->pdata = zalloc(sizeof(*chn->pdata));
	if (!chn->pdata)
		goto err_free_chn;

	if (!strncmp(attr, "out_", 4)) {
		chn->is_output = true;
	} else if (strncmp(attr, "in_", 3)) {
		err = -EINVAL;
		goto err_free_chn_pdata;
	}

	chn->dev = dev;
	chn->id = id;
	chn->is_scan_element = is_scan_element;
	chn->index = -ENOENT;

	err = add_attr_to_channel(chn, attr, path, is_scan_element);
	if (err)
		goto err_free_chn_pdata;

	return chn;

err_free_chn_pdata:
	free(chn->pdata->enable_fn);
	free(chn->pdata);
err_free_chn:
	free(chn);
	return iio_ptr(err);
}
static const char * const iio_chan_type_name_spec[] = {
	[IIO_VOLTAGE] = "voltage",
	[IIO_CURRENT] = "current",
	[IIO_POWER] = "power",
	[IIO_ACCEL] = "accel",
	[IIO_ANGL_VEL] = "anglvel",
	[IIO_MAGN] = "magn",
	[IIO_LIGHT] = "illuminance",
	[IIO_INTENSITY] = "intensity",
	[IIO_PROXIMITY] = "proximity",
	[IIO_TEMP] = "temp",
	[IIO_INCLI] = "incli",
	[IIO_ROT] = "rot",
	[IIO_ANGL] = "angl",
	[IIO_TIMESTAMP] = "timestamp",
	[IIO_CAPACITANCE] = "capacitance",
	[IIO_ALTVOLTAGE] = "altvoltage",
	[IIO_CCT] = "cct",
	[IIO_PRESSURE] = "pressure",
	[IIO_HUMIDITYRELATIVE] = "humidityrelative",
	[IIO_ACTIVITY] = "activity",
	[IIO_STEPS] = "steps",
	[IIO_ENERGY] = "energy",
	[IIO_DISTANCE] = "distance",
	[IIO_VELOCITY] = "velocity",
	[IIO_CONCENTRATION] = "concentration",
	[IIO_RESISTANCE] = "resistance",
	[IIO_PH] = "ph",
	[IIO_UVINDEX] = "uvindex",
	[IIO_ELECTRICALCONDUCTIVITY] = "electricalconductivity",
	[IIO_COUNT] = "count",
	[IIO_INDEX] = "index",
	[IIO_GRAVITY] = "gravity",
	[IIO_POSITIONRELATIVE] = "positionrelative",
	[IIO_PHASE] = "phase",
	[IIO_MASSCONCENTRATION] = "massconcentration",
	[IIO_DELTA_ANGL] = "delta_angl",
	[IIO_DELTA_VELOCITY] = "delta_velocity",
	[IIO_COLORTEMP] = "colortemp",
	[IIO_CHROMATICITY] = "chromaticity",
	[IIO_ATTENTION] = "attention",
};

static const char * const modifier_names[] = {
	[IIO_MOD_X] = "x",
	[IIO_MOD_Y] = "y",
	[IIO_MOD_Z] = "z",
	[IIO_MOD_X_AND_Y] = "x&y",
	[IIO_MOD_X_AND_Z] = "x&z",
	[IIO_MOD_Y_AND_Z] = "y&z",
	[IIO_MOD_X_AND_Y_AND_Z] = "x&y&z",
	[IIO_MOD_X_OR_Y] = "x|y",
	[IIO_MOD_X_OR_Z] = "x|z",
	[IIO_MOD_Y_OR_Z] = "y|z",
	[IIO_MOD_X_OR_Y_OR_Z] = "x|y|z",
	[IIO_MOD_ROOT_SUM_SQUARED_X_Y] = "sqrt(x^2+y^2)",
	[IIO_MOD_SUM_SQUARED_X_Y_Z] = "x^2+y^2+z^2",
	[IIO_MOD_LIGHT_BOTH] = "both",
	[IIO_MOD_LIGHT_IR] = "ir",
	[IIO_MOD_LIGHT_CLEAR] = "clear",
	[IIO_MOD_LIGHT_RED] = "red",
	[IIO_MOD_LIGHT_GREEN] = "green",
	[IIO_MOD_LIGHT_BLUE] = "blue",
	[IIO_MOD_LIGHT_UV] = "uv",
	[IIO_MOD_LIGHT_DUV] = "duv",
	[IIO_MOD_QUATERNION] = "quaternion",
	[IIO_MOD_TEMP_AMBIENT] = "ambient",
	[IIO_MOD_TEMP_OBJECT] = "object",
	[IIO_MOD_NORTH_MAGN] = "from_north_magnetic",
	[IIO_MOD_NORTH_TRUE] = "from_north_true",
	[IIO_MOD_NORTH_MAGN_TILT_COMP] = "from_north_magnetic_tilt_comp",
	[IIO_MOD_NORTH_TRUE_TILT_COMP] = "from_north_true_tilt_comp",
	[IIO_MOD_RUNNING] = "running",
	[IIO_MOD_JOGGING] = "jogging",
	[IIO_MOD_WALKING] = "walking",
	[IIO_MOD_STILL] = "still",
	[IIO_MOD_ROOT_SUM_SQUARED_X_Y_Z] = "sqrt(x^2+y^2+z^2)",
	[IIO_MOD_I] = "i",
	[IIO_MOD_Q] = "q",
	[IIO_MOD_CO2] = "co2",
	[IIO_MOD_ETHANOL] = "ethanol",
	[IIO_MOD_H2] = "h2",
	[IIO_MOD_O2] = "o2",
	[IIO_MOD_VOC] = "voc",
	[IIO_MOD_PM1] = "pm1",
	[IIO_MOD_PM2P5] = "pm2p5",
	[IIO_MOD_PM4] = "pm4",
	[IIO_MOD_PM10] = "pm10",
	[IIO_MOD_LINEAR_X] = "linear_x",
	[IIO_MOD_LINEAR_Y] = "linear_y",
	[IIO_MOD_LINEAR_Z] = "linear_z",
	[IIO_MOD_PITCH] = "pitch",
	[IIO_MOD_YAW] = "yaw",
	[IIO_MOD_ROLL] = "roll",
	[IIO_MOD_LIGHT_UVA] = "uva",
	[IIO_MOD_LIGHT_UVB] = "uvb",
};

static int iio_channel_find_type(const char *id,
			const char *const *name_spec, size_t size)
{
	unsigned int i;
	size_t len;

	for (i = 0; i < size; i++) {
		len = strlen(name_spec[i]);
		if (strncmp(name_spec[i], id, len) != 0)
		      continue;

		/* Type must be followed by one of a '\0', a '_', or a digit */
		if (id[len] != '\0' && id[len] != '_' &&
				(id[len] < '0' || id[len] > '9'))
			continue;

		return i;
	}

	return -EINVAL;
}

void iio_channel_init_finalize(struct iio_channel *chn)
{
	unsigned int i;
	size_t len;
	char *mod;
	int type;


	type = iio_channel_find_type(chn->id, iio_chan_type_name_spec,
				ARRAY_SIZE(iio_chan_type_name_spec));


	chn->type = (type >= 0) ? type : IIO_CHAN_TYPE_UNKNOWN;
	chn->modifier = IIO_NO_MOD;

	mod = strchr(chn->id, '_');
	if (!mod)
		return;

	mod++;

	for (i = 0; i < ARRAY_SIZE(modifier_names); i++) {
		if (!modifier_names[i])
			continue;
		len = strlen(modifier_names[i]);
		if (strncmp(modifier_names[i], mod, len) != 0)
			continue;

		chn->modifier = (enum iio_modifier) i;
		break;
	}
}

static int add_channel(struct iio_device *dev, const char *name,
	const char *path, bool dir_is_scan_elements)
{
	struct iio_channel *chn;
	char *channel_id;
	unsigned int i;
	int ret;

	channel_id = get_channel_id(dev, name);
	if (!channel_id)
		return -ENOMEM;

	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];
		if (!strcmp(chn->id, channel_id)
				&& chn->is_output == (name[0] == 'o')) {
			free(channel_id);
			ret = add_attr_to_channel(chn, name, path,
					dir_is_scan_elements);
			chn->is_scan_element |= dir_is_scan_elements && !ret;
			return ret;
		}
	}

	chn = create_channel(dev, channel_id, name, path, dir_is_scan_elements);
	// ret = iio_err(chn);
	// if (ret) {
	// 	free(channel_id);
	// 	return ret;
	// }

	iio_channel_init_finalize(chn);

	ret = add_channel_to_device(dev, chn);
	// if (ret) {
	// 	local_free_channel_pdata(chn);
	// 	//free_channel(chn);
	// }
	return ret;
}
static int add_attr_or_channel_helper(struct iio_device *dev,
		const char *path, const char *prefix,
		bool dir_is_scan_elements)
{
	char buf[1024];
	const char *name = strrchr(path, '/') + 1;

	if (!dir_is_scan_elements && !is_channel(dev, name, true))
	      return add_attr_to_device(dev, name);

	snprintf(buf, sizeof(buf), "%s%s", prefix, name);

	return add_channel(dev, name, buf, dir_is_scan_elements);
}

static int add_attr_or_channel(void *d, const char *path)
{
	return add_attr_or_channel_helper((struct iio_device *) d,
				path, "", false);
}

int find_iio_device_attr_or_channel(struct iio_context *ctx, void *d, const char *path, 
						int (*callback)(void *, const char *))
{
    if (!ctx) {
        return -EINVAL;
    }
	char buf[PATH_MAX];
	char c_name[20] = "out_voltage0_raw"; // Example attribute name
	snprintf(buf, sizeof(buf), "%s/%s", path, c_name);
	uint32_t ret = 0;
	ret = callback(d, buf); //add_attr_or_channel
    
    return ret;
}
static int create_device(void *d, const char *path)
{
	unsigned int i;
	int ret;
	struct iio_context *ctx = d;
	struct iio_device *dev;
	const char *id, *name_ptr = NULL, *label_ptr = NULL;
	char name[512], label[512];

	id = strrchr(path, '/') + 1; //points to adxl345

// 	ret = (int)local_do_read_dev_attr(id, 0, "name", name, sizeof(name),
// 					  IIO_ATTR_TYPE_DEVICE);
// 	if (ret > 0)
// 		name_ptr = name;
	name_ptr = id;//"ADXL345"; // For testing purposes, we set a static name
// 	ret = (int)local_do_read_dev_attr(id, 0, "label", label, sizeof(label),
// 					  IIO_ATTR_TYPE_DEVICE);
// 	if (ret > 0)
// 		label_ptr = label;
	label_ptr = id;// "ADXL345"; // For testing purposes, we set a static label

	// Create the IIO device with the given id, name, and label
	dev = iio_context_add_device(ctx, id, name_ptr, label_ptr);
	if (!dev)
		return -ENOMEM;

// 	ret = foreach_in_dir(ctx, dev, path, false, add_attr_or_channel);
// 	if (ret < 0)
// 		goto err_free_device;
	ret = find_iio_device_attr_or_channel(ctx, dev, path ,add_attr_or_channel);
// 	ret = add_buffer_attributes(dev, path);
// 	if (ret < 0)
// 		goto err_free_device;

// 	ret = add_events(dev, path);
// 	if (ret < 0)
// 		goto err_free_scan_elements;

// 	ret = add_scan_elements(dev, path);
// 	if (ret < 0)
// 		goto err_free_scan_elements;

// 	for (i = 0; i < dev->nb_channels; i++) {
// 		struct iio_channel *chn = dev->channels[i];

// 		ret = set_channel_name(chn);
// 		if (ret < 0)
// 			goto err_free_scan_elements;

// 		ret = handle_scan_elements(chn);
// 		free_protected_attrs(chn);
// 		if (ret < 0)
// 			goto err_free_scan_elements;
// 	}

// 	ret = detect_and_move_global_attrs(dev);
// 	if (ret < 0)
// 		goto err_free_device;

// 	/* sorting is done after global attrs are added */
// 	for (i = 0; i < dev->nb_channels; i++)
// 		iio_sort_attrs(&dev->channels[i]->attrlist);

// 	iio_sort_attrs(&dev->attrlist[IIO_ATTR_TYPE_DEVICE]);

// 	return 0;

// err_free_scan_elements:
// 	for (i = 0; i < dev->nb_channels; i++)
// 		free_protected_attrs(dev->channels[i]);
// err_free_device:
// 	local_free_pdata(dev);
// 	free_device(dev);
// 	return ret;
} 

int find_iio_device_names(struct iio_context *ctx, void *d, const char *path, 
						int (*callback)(void *, const char *)) 
{
    if (!ctx) {
        return -EINVAL;
    }
	char buf[PATH_MAX];
	char d_name[23] = "cf-ad9361-dds-core-lpc";
	snprintf(buf, sizeof(buf), "%s/%s", path, d_name);
	uint32_t ret = 0;
	ret = callback(d, buf);
	
	char d_name1[11] = "ad9361-phy";
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "/%s", d_name1);
	ret = callback(d, buf); //create_device
    return ret;
}

struct iio_context_pdata {
	struct iio_mutex *lock;
};

static const struct iio_context_params default_params = {
	.timeout_ms = 0,

	.out = NULL, /* stdout */
	.err = NULL, /* stderr */
	.log_level = (enum iio_log_level)LEVEL_NOLOG,
	.stderr_level = LEVEL_WARNING,
	.timestamp_level = LEVEL_DEBUG,
};
#define FQDN_LEN (255) 
char * iio_getenv (char * envvar)
{
	char *hostname;
	size_t len, tmp;

#ifdef _MSC_BUILD
	if (_dupenv_s(&hostname, NULL, envvar))
		return NULL;
#else
	/* This is qualified below, and a copy is returned
	 * so it's safe to use
	 */
	hostname =  "win";//getenv(envvar); /* Flawfinder: ignore */
#endif

	if (!hostname)
		return NULL;

	tmp = FQDN_LEN + sizeof("serial:") + sizeof(":65535") - 2;
	len = strnlen(hostname, tmp);

	/* Should be smaller than max length */
	if (len == tmp)
		goto wrong_str;

	/* should be more than "usb:" or "ip:" */
	tmp = sizeof("ip:") - 1;
	if (len < tmp)
		goto wrong_str;

#ifdef _MSC_BUILD
	return hostname;
#else
	return "win";//strdup (hostname);
#endif

wrong_str:
#ifdef _WIN32
	free(hostname);
#endif
	return NULL;
}

static char * local_get_description(const struct iio_context *ctx)
{
	char *description;
	unsigned int len;
	struct utsname uts;

	uname(&uts);
	len = strlen(uts.sysname) + strlen(uts.nodename) + strlen(uts.release)
		+ strlen(uts.version) + strlen(uts.machine);
	description = malloc(len + 5); /* 4 spaces + EOF */
	if (!description)
		return NULL;

    snprintf(description, len + 5, "%s %s %s %s %s", uts.sysname,
		uts.nodename, uts.release, uts.version, uts.machine);

	//iio_snprintf(description, len + 5, "%s %s %s %s %s", uts.sysname,
	//		uts.nodename, uts.release, uts.version, uts.machine);

	return description;
}

struct iio_context *
iio_context_create_from_backend(const struct iio_context_params *params,
				const struct iio_backend *backend,
				const char *description,
				unsigned int major, unsigned int minor,
				const char *git_tag)
{
	struct iio_context *ctx;
	int ret = -ENOMEM;

	if (!backend)
		return iio_ptr(-EINVAL);

	ctx = zalloc(sizeof(*ctx));
	if (!ctx)
		return iio_ptr(-ENOMEM);

	if (description) {
		ctx->description = iio_strdup(description);
		if (!ctx->description)
			goto err_free_ctx;
	}

	ctx->name = backend->name;
	ctx->ops = backend->ops;
	//ctx->params = *params;

	ctx->major = major;
	ctx->minor = minor;

	// if (git_tag) {
	// 	ctx->git_tag = iio_strdup(git_tag);
	// 	if (!ctx->git_tag)
	// 		goto err_free_description;
	// }

	return ctx;

err_free_description:
	free(ctx->description);
err_free_ctx:
	free(ctx);
	return iio_ptr(ret);
}

static struct iio_context *
local_create_context(const struct iio_context_params *params, const char *args)
{
	struct iio_context *ctx;
	char *description;
	int ret = -ENOMEM;
	struct utsname uts;
	bool no_iio;

	description = local_get_description(NULL);
	if (!description)
		return iio_ptr(-ENOMEM);

	ctx = iio_context_create_from_backend(params, &iio_local_backend,
					      description, 0, 0, NULL);
	free(description);
	ret = iio_err(ctx);
	if (ret)
		return iio_err_cast(ctx);

	ctx->pdata = calloc(1, sizeof(*ctx->pdata));
	if (!ctx->pdata) {
		ret = -ENOMEM;
		goto err_context_destroy;
	}

	// ctx->pdata->lock = iio_mutex_create();
	// ret = iio_err(ctx->pdata->lock);
	// if (ret < 0)
	// 	goto err_context_destroy;

	//ret = foreach_in_dir(ctx, ctx, "/sys/bus/iio/devices",
	//		     true, create_device);

	ret = find_iio_device_names(ctx, ctx, "/sys/bus/iio/devices" ,create_device);

// 	no_iio = ret == -ENOENT;
// 	if (WITH_HWMON && no_iio)
// 	      ret = 0; /* Not an error, unless we also have no hwmon devices */
// 	if (ret < 0)
// 	      goto err_context_destroy;

// 	if (WITH_HWMON) {
// 		ret = foreach_in_dir(ctx, ctx, "/sys/class/hwmon",
// 				     true, create_device);
// 		if (ret == -ENOENT && !no_iio)
// 			ret = 0; /* IIO devices but no hwmon devices - not an error */
// 		if (ret < 0)
// 			goto err_context_destroy;
// 	}

// 	iio_sort_devices(ctx);

// 	foreach_in_dir(ctx, ctx, "/sys/kernel/debug/iio", true, add_debug);

// 	if (WITH_LOCAL_CONFIG) {
// 		ret = populate_context_attrs(ctx, "/etc/libiio.ini");
// 		if (ret < 0)
// 			prm_warn(params, "Unable to read INI file: %d\n", ret);
// 	}

// 	uname(&uts);
// 	ret = iio_context_add_attr(ctx, "local,kernel", uts.release);
// 	if (ret < 0)
// 		goto err_context_destroy;

// 	ret = iio_context_add_attr(ctx, "uri", "local:");
// 	if (ret < 0)
// 		goto err_context_destroy;

// 	ret = iio_context_init(ctx);
// 	if (ret < 0)
// 		goto err_context_destroy;

// 	ret = init_devices(ctx);
// 	if (ret < 0)
// 		goto err_context_destroy;

 	return ctx;

err_context_destroy:
	//iio_context_destroy(ctx);
	return iio_ptr(ret);
}

struct iio_buffer_pdata {
	const struct iio_device *dev;
	struct iio_buffer_impl_pdata *pdata;
	int fd, cancel_fd;
	struct iio_buffer_params *params;
	bool dmabuf_supported;
	bool mmap_supported;
	size_t size;
};

typedef  __UINT_FAST64_TYPE__ atomic_uint_fast64_t;

struct iio_buffer_impl_pdata {
	atomic_uint_fast64_t mmap_block_mask;
	atomic_uint_fast64_t mmap_enqueued_blocks_mask;
	bool mmap_check_done;
	bool cyclic_buffer_enqueued;
	unsigned int nb_blocks;
};

struct iio_buffer_impl_pdata * local_alloc_mmap_buffer_impl(void)
{
	struct iio_buffer_impl_pdata *pdata;

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		return iio_ptr(-ENOMEM);

	return pdata;
}

static int channel_write_state(const struct iio_channel *chn,
			       unsigned int idx, bool en)
{
	// enum iio_attr_type type = idx ? IIO_ATTR_TYPE_BUFFER : IIO_ATTR_TYPE_DEVICE;
	// ssize_t ret;

	// if (!chn->pdata->enable_fn) {
	// 	chn_err(chn, "Libiio bug: No \"en\" attribute parsed\n");
	// 	return -EINVAL;
	// }

	// ret = local_write_dev_attr(chn->dev, idx, chn->pdata->enable_fn,
	// 			   en ? "1" : "0", 2, type);
	// if (ret < 0)
	// 	return (int) ret;
	// else
	// 	return 0;
}

static int channel_read_state(const struct iio_channel *chn, unsigned int idx)
{
	// enum iio_attr_type type = idx ? IIO_ATTR_TYPE_BUFFER : IIO_ATTR_TYPE_DEVICE;
	// char buf[8];
	// int err;

	// err = local_read_dev_attr(chn->dev, idx, chn->pdata->enable_fn,
	// 			  buf, sizeof(buf), type);
	// if (err < 0)
	// 	return err;

	// return buf[0] == '1';
}
static int local_do_enable_buffer(struct iio_buffer_pdata *pdata, bool enable)
{
	// int ret;

	// ret = (int) local_write_dev_attr(pdata->dev, pdata->params->idx, "enable",
	// 				 enable ? "1" : "0",
	// 				 2, IIO_ATTR_TYPE_BUFFER);
	// if (ret < 0)
	// 	return ret;

	// return 0;
}

bool iio_channel_is_enabled(const struct iio_channel *chn,
			    const struct iio_channels_mask *mask)
{
	return chn->index >= 0 && iio_channels_mask_test_bit(mask, chn->number);
}

static struct iio_buffer_pdata *
local_create_buffer(const struct iio_device *dev,
		    struct iio_buffer_params *params,
		    struct iio_channels_mask *mask)
{
	struct iio_buffer_pdata *pdata;
	const struct iio_channel *chn;
	int err, cancel_fd, fd;
	unsigned int i;

	pdata = zalloc(sizeof(*pdata));
	if (!pdata)
		return iio_ptr(-ENOMEM);

	pdata->dev = dev;

	//if (WITH_LOCAL_MMAP_API) {
		pdata->pdata = local_alloc_mmap_buffer_impl();
		// err = iio_err(pdata->pdata);
		// if (err)
		// 	goto err_free_pdata;
	//}

	// cancel_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	// if (cancel_fd == -1) {
	// 	err = -errno;
	// 	goto err_free_mmap_pdata;
	// }

	// err = local_open_fd(dev, false, params->idx);
	// if (err < 0)
	// 	goto err_close_eventfd;

	// fd = err;

	pdata->params = params;
	pdata->cancel_fd = cancel_fd;
	pdata->fd = fd;

	/* Disable buffer */
	err = local_do_enable_buffer(pdata, false);
	// if (err < 0)
	// 	goto err_close;

	/* Disable all channels */
	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];
		if (chn->index >= 0) {
			err = channel_write_state(chn, params->idx, false);
			// if (err < 0)
			// 	goto err_close;
		}
	}

	/* Enable channels */
	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];
		if (chn->index >= 0 && iio_channel_is_enabled(chn, mask)) {
			err = channel_write_state(chn, params->idx, true);
			// if (err < 0)
			// 	goto err_close;
		}
	}

	/* Finally, update the channels mask by reading the hardware again,
	 * since some channels may be coupled together. */
	for (i = 0; i < dev->nb_channels; i++) {
		chn = dev->channels[i];
		if (chn->index >= 0) {
			err = channel_read_state(chn, params->idx);
			if (err < 0)
				//goto err_close;

			if (err > 0)
				iio_channel_enable(chn, mask);
		}
	}

	return pdata;

// err_close:
// 	local_close_fd(dev, fd);
// err_close_eventfd:
// 	close(cancel_fd);
// err_free_mmap_pdata:
// 	free(pdata->pdata);
// err_free_pdata:
// 	free(pdata);
// 	return iio_ptr(err);
}

void iio_channel_enable(const struct iio_channel *chn,
			struct iio_channels_mask *mask)
{
	printk("Enable");
	bool i, y = false;
	i = !(chn->is_scan_element); /* reveerse logic because is scan element is not set ?*/
	y = ~chn->index >= 0;/* reveerse logic because is scan element is not set ?*/
	if (i && y) 
		iio_channels_mask_set_bit(mask, chn->number);
	// if ((!(chn->is_scan_element)) && chn->index >= 0) /* reveerse logic because is scan element is not set ?*/
	// 	iio_channels_mask_set_bit(mask, chn->number);
}

// struct iio_buffer_impl_pdata;
// struct iio_block_impl_pdata;
// struct iio_device;
// struct iio_buffer_params;
// struct timespec;

// struct iio_block_pdata {
// 	struct iio_buffer_pdata *buf;
// 	struct iio_block_impl_pdata *pdata;
// 	size_t size;
// 	void *data;
// 	bool dequeued;
// 	bool cpu_access_disabled;
// };

// struct iio_block {
// 	struct iio_buffer *buffer;
// 	struct iio_block_pdata *pdata;
// 	size_t size;
// 	void *data;

// 	struct iio_task_token *token;
// 	size_t bytes_used;

// 	int dmabuf_fd;
// };

// int iio_block_io(struct iio_block *block)
// {
// 	if (!iio_device_is_tx(block->buffer->dev))
// 		return iio_block_read(block);

// 	return iio_block_write(block);
// }

static int iio_buffer_enqueue_worker(void *_, void *d)
{
	//return iio_block_io(d);
	return 1;
}

struct iio_task {
	// struct iio_thrd *thrd;
	// struct iio_cond *cond;
	// struct iio_mutex *lock;
	int (*fn)(void *, void *);
	void *firstarg;

	// struct iio_task_token *list;
	bool running, stop;
};

struct iio_task * iio_task_create(int (*fn)(void *, void *),
				  void *firstarg, const char *name)
{
// 	struct iio_task *task;
// 	int err = -ENOMEM;

// 	task = calloc(1, sizeof(*task));
// 	if (!task)
// 		return iio_ptr(-ENOMEM);

// 	task->lock = iio_mutex_create();
// 	err = iio_err(task->lock);
// 	if (err)
// 		goto err_free_task;

// 	task->cond = iio_cond_create();
// 	err = iio_err(task->cond);
// 	if (err)
// 		goto err_free_lock;

// 	task->fn = fn;
// 	task->firstarg = firstarg;

// 	if (!NO_THREADS) {
// 		task->thrd = iio_thrd_create(iio_task_run, task, name);
// 		err = iio_err(task->thrd);
// 		if (err)
// 			goto err_free_cond;
// 	}

// 	return task;

// err_free_cond:
// 	iio_cond_destroy(task->cond);
// err_free_lock:
// 	iio_mutex_destroy(task->lock);
// err_free_task:
// 	free(task);
// 	return iio_ptr(err);
}

int iio_channels_mask_copy(struct iio_channels_mask *dst,
			   const struct iio_channels_mask *src)
{
	if (dst->words != src->words)
		return -EINVAL;

	memcpy(dst->mask, src->mask, src->words * sizeof(uint32_t));

	return 0;
}

struct iio_buffer *
iio_device_create_buffer(const struct iio_device *dev,
			 struct iio_buffer_params *params,
			 const struct iio_channels_mask *mask)
{
	const struct iio_backend_ops *ops = dev->ctx->ops;
	struct iio_buffer *buf;
	size_t sample_size;
	size_t attrlist_size;
	unsigned int i;
	int err;

	if (!ops->create_buffer)
		return iio_ptr(-ENOSYS);

	sample_size = 4;//iio_device_get_sample_size(dev, mask);
// 	if (sample_size < 0)
// 		return iio_ptr((int) sample_size);
// 	if (!sample_size)
// 		return iio_ptr(-EINVAL);

	buf = zalloc(sizeof(*buf));
// 	if (!buf)
// 		return iio_ptr(-ENOMEM);

// 	if (params) {//params are null from main
// 		/* We need to make sure that all reserved bytes are zero initialized.
// 		 * This is important for the ABI stability.
// 		 */
// 		for (i = 0; i < sizeof(buf->params.__rsrv); i++) {
// 			if (params->__rsrv[i]) {
// 				dev_err(dev, "Reserved bytes in buffer params must be zero.\n");
// 				err = -E2BIG;
// 				goto err_free_buf;
// 			}
// 		}

// 		buf->params = *params;
// 	}
	buf->dev = dev;

	/* Duplicate buffer attributes from the iio_device.
	 * This ensures that those can contain a pointer to our iio_buffer */
	buf->attrlist.num = dev->attrlist[IIO_ATTR_TYPE_BUFFER].num;
	attrlist_size = buf->attrlist.num * sizeof(*buf->attrlist.attrs);
	buf->attrlist.attrs = malloc(attrlist_size);
	// if (!buf->attrlist.attrs) {
	// 	err = -ENOMEM;
	// 	goto err_free_buf;
	// }

	memcpy(buf->attrlist.attrs, /* Flawfinder: ignore */
	       dev->attrlist[IIO_ATTR_TYPE_BUFFER].attrs, attrlist_size);

	for (i = 0; i < buf->attrlist.num; i++)
		buf->attrlist.attrs[i].iio.buf = buf;

	buf->mask = iio_create_channels_mask(dev->nb_channels);
	// if (!buf->mask) {
	// 	err = -ENOMEM;
	// 	goto err_free_attrs;
	// }

	err = iio_channels_mask_copy(buf->mask, mask);
// 	if (err)
// 		goto err_free_mask;

// 	buf->lock = iio_mutex_create();
// 	err = iio_err(buf->lock);
// 	if (err)
// 		goto err_free_mask;

	buf->worker = iio_task_create(iio_buffer_enqueue_worker, NULL,
				      "enqueue-worker");
// 	err = iio_err(buf->worker);
// 	if (err < 0)
// 		goto err_free_mutex;

	buf->pdata = ops->create_buffer(dev, &buf->params, buf->mask); //local_create_buffer
// 	err = iio_err(buf->pdata);
// 	if (err < 0)
// 		goto err_destroy_worker;

	return buf;

// err_destroy_worker:
// 	iio_task_destroy(buf->worker);
// err_free_mutex:
// 	iio_mutex_destroy(buf->lock);
// err_free_mask:
// 	iio_channels_mask_destroy(buf->mask);
// err_free_attrs:
// 	/* No need to call iio_free_attrs() since the names / filenames
// 	 * are allocated by the device */
// 	free(buf->attrlist.attrs);
// err_free_buf:
// 	free(buf);
// 	return iio_ptr(err);
    return NULL; // Placeholder return value
}

static struct iio_block_pdata *
local_create_block(struct iio_buffer_pdata *pdata, size_t size, void **data)
{
	struct iio_block_pdata *block;
	int ret;

	// if (WITH_LOCAL_DMABUF_API) {
	// 	block = local_create_dmabuf(pdata, size, data);
	// 	ret = iio_err(block);

	// 	if (ret != -ENOSYS)
	// 		return block;
	// }

	// if (WITH_LOCAL_MMAP_API) {
		block = NULL;//local_create_mmap_block(pdata, size, data);
		ret = iio_err(block);

		if (ret != -ENOSYS)
			return block;
	// }

	// return iio_ptr(-ENOSYS);
}
int iio_backend_read(const struct iio_dev *iio_dev, struct iio_dev_attr *attr,
                                char *buf)
{
	return 0; // Placeholder implementation
}

static const struct iio_backend_ops local_ops = {
	// .scan = local_context_scan,
	.create = local_create_context,
	// .read_attr = local_read_attr,
	// .write_attr = local_write_attr,
	// .get_trigger = local_get_trigger,
	// .set_trigger = local_set_trigger,
	// .shutdown = local_shutdown,

	 .create_block = local_create_block,
	// .free_block = local_free_block,
	// .enqueue_block = local_enqueue_block,
	// .dequeue_block = local_dequeue_block,

	.create_buffer = local_create_buffer,
	// .free_buffer = local_free_buffer,
	// .enable_buffer = local_enable_buffer,
	// .cancel_buffer = local_cancel_buffer,

	.backend_read = iio_backend_read,
	// .readbuf = local_readbuf,
	// .writebuf = local_writebuf,

	// .open_ev = local_open_events_fd,
	// .close_ev = local_close_events_fd,
	// .read_ev = local_read_event,

	// .get_dmabuf_fd = local_get_dmabuf_fd,
	// .disable_cpu_access = local_disable_cpu_access,
};

const struct iio_backend iio_local_backend = {
	.api_version = IIO_BACKEND_API_V1,
	.name = "local",
	.uri_prefix = "local:",
	.ops = &local_ops,
	.default_timeout_ms = 1000,
};

const struct iio_backend * const iio_backends[] = {
//#ifdef WITH_LOCAL_BACKEND
	&iio_local_backend,
//#endif
#ifdef WITH_NETWORK_BACKEND
	IF_ENABLED(WITH_NETWORK_BACKEND && !WITH_NETWORK_BACKEND_DYNAMIC,
		   (&iio_ip_backend)),
#endif
#ifdef WITH_SERIAL_BACKEND
	IF_ENABLED(WITH_SERIAL_BACKEND && !WITH_SERIAL_BACKEND_DYNAMIC,
		   (&iio_serial_backend)),
#endif
#ifdef WITH_USB_BACKEND
	IF_ENABLED(WITH_USB_BACKEND && !WITH_USB_BACKEND_DYNAMIC,
		   (&iio_usb_backend)),
#endif
#ifdef WITH_XML_BACKEND
	IF_ENABLED(WITH_XML_BACKEND, (&iio_xml_backend)),
#endif
};
const unsigned int iio_backends_size = ARRAY_SIZE(iio_backends);

struct iio_context * iio_create_context(const struct iio_context_params *params,
					const char *uri)
{
	struct iio_context_params params2 = { 0 };
	const struct iio_backend *backend = NULL;
	struct iio_context *ctx = NULL;
	char *uri_dup = NULL;
	unsigned int i;
	int err;

    err = iio_backends[0]->default_timeout_ms;
	if (params)
		params2 = *params;

	if (!params2.log_level)
		params2.log_level = default_params.log_level;
	if (!params2.stderr_level)
		params2.stderr_level = default_params.stderr_level;
	if (!params2.timestamp_level)
		params2.timestamp_level = default_params.timestamp_level;

	if (!uri) {
		//uri_dup = iio_getenv("IIOD_REMOTE"); //commented to support local for now

		uri = uri_dup ? uri_dup : "local:";
	}

	for (i = 0; !backend && i < ARRAY_SIZE(iio_backends); i++) {
		if (!iio_backends[i])
			continue;

		if (!strncmp(uri, iio_backends[i]->uri_prefix,
			     strlen(iio_backends[i]->uri_prefix))) {
			backend = iio_backends[i];
		}
	}

	if (backend) {
		if (!params2.timeout_ms)
			params2.timeout_ms = backend->default_timeout_ms;

		ctx = backend->ops->create(&params2, /*local_create_context*/
					   uri + strlen(backend->uri_prefix)); //TODO create create ops
	//} else if (WITH_MODULES) {
		//ctx = iio_create_dynamic_context(&params2, uri);
	} else {
		//ctx = iio_ptr(-ENODEV);
	}
	// ctx->nb_devices = 1;
    // printf("asd %d", ctx);
	free(uri_dup);

	// if (!iio_err(ctx)) {
	// 	err = iio_context_update_scale_offset(ctx);
	// 	if (err) {
	// 		iio_context_destroy(ctx);
	// 		ctx = iio_ptr(err);
	// 	}
	// }

	return ctx;
    return NULL; // Placeholder return value
}

unsigned int iio_context_get_devices_count(const struct iio_context *ctx)
{
	ctx->nb_devices;
}

struct iio_device * iio_context_find_device(const struct iio_context *ctx,
		const char *name)
{
	unsigned int i;
	for (i = 0; i < ctx->nb_devices; i++) {
		struct iio_device *dev = ctx->devices[i];
		if (!strcmp(dev->id, name) ||
		    (dev->label && !strcmp(dev->label, name)) ||
		    (dev->name && !strcmp(dev->name, name)))
			return dev;
	}
	return NULL;
}

const struct iio_attr *
iio_channel_find_attr(const struct iio_channel *chn, const char *name)
{
	const struct iio_attr *attr;
	size_t len;

	// attr = iio_attr_find(&chn->attrlist, name);
	// if (attr)
	// 	return attr;

	// /* Support attribute names that start with the channel's label to avoid
	//  * breaking compatibility with old kernels, which did not offer a
	//  * 'label' attribute, and caused Libiio to sometimes misdetect the
	//  * channel's extended name as being part of the attribute name. */
	// if (chn->name) {
	// 	len = strlen(chn->name);

	// 	if (!strncmp(chn->name, name, len) && name[len] == '_') {
	// 		name += len + 1;
	// 		return iio_attr_find(&chn->attrlist, name);
	// 	}
	// }

	return NULL;
}


size_t iio_attr_write_string(const struct iio_attr *attr, const char *src)
{
	return 0; //iio_attr_write_raw(attr, src, strlen(src) + 1); /* Flawfinder: ignore */
}

struct iio_channels_mask * iio_create_channels_mask(unsigned int nb_channels)
{
	struct iio_channels_mask *mask;
	size_t nb_words = (nb_channels + 31) / 32;

	if (!nb_words)
		return NULL;

	mask = zalloc(sizeof(*mask) + nb_words * sizeof(uint32_t));
	if (mask)
		mask->words = nb_words;

	return mask;
}

unsigned int iio_device_get_channels_count(const struct iio_device *dev)
{
	return dev->nb_channels;
}

struct iio_stream {
	struct iio_buffer *buffer;
	struct iio_block **blocks;
	size_t nb_blocks;
	bool started, buf_enabled, all_enqueued;
	unsigned int curr;
};

struct iio_block {
	struct iio_buffer *buffer;
	struct iio_block_pdata *pdata;
	size_t size;
	void *data;

	struct iio_task_token *token;
	size_t bytes_used;

	int dmabuf_fd;
};
struct iio_block *
iio_buffer_create_block(struct iio_buffer *buf, size_t size)
{
	const struct iio_device *dev = buf->dev;
	const struct iio_backend_ops *ops = dev->ctx->ops;
	struct iio_block_pdata *pdata;
	size_t sample_size;
	struct iio_block *block;
	int ret;

	sample_size = iio_device_get_sample_size(dev, buf->mask);
	if (sample_size == 0 || size < sample_size)
		return iio_ptr(-EINVAL);

	block = zalloc(sizeof(*block));
	if (!block)
		return iio_ptr(-ENOMEM);

	block->dmabuf_fd = -EINVAL;

	if (ops->create_block) {
		pdata = ops->create_block(buf->pdata, size, &block->data);//local_create_block
		ret = iio_err(pdata);
		if (!ret) {
			block->pdata = pdata;

			if (ops->get_dmabuf_fd)
				block->dmabuf_fd = ops->get_dmabuf_fd(pdata);
		} else if (ret != -ENOSYS) {
			// goto err_free_block;
		}
	}

	if (!block->pdata) {
		block->data = malloc(size);
		if (!block->data) {
			ret = -ENOMEM;
			// goto err_free_block;
		}

		if (size > buf->length)
		      buf->length = size;

		buf->block_size = size;
	}

	block->buffer = buf;
	block->size = size;

	// iio_mutex_lock(buf->lock);
	buf->nb_blocks++;
	// iio_mutex_unlock(buf->lock);

	return block;

// err_free_block:
// 	free(block);
// 	return iio_ptr(ret);
}

struct iio_stream *
iio_buffer_create_stream(struct iio_buffer *buffer, size_t nb_blocks,
			 size_t samples_count)
{
	struct iio_stream *stream;
	size_t i, sample_size, buf_size;
	int err;

// 	if (!nb_blocks || !samples_count)
// 		return iio_ptr(-EINVAL);

	stream = zalloc(sizeof(*stream));
// 	if (!stream)
// 		return iio_ptr(-ENOMEM);

	stream->blocks = calloc(1 /*nb_blocks*/, sizeof(*stream->blocks));
// 	if (!stream->blocks) {
// 		err = -ENOMEM;
// 		goto err_free_stream;
// 	}

	sample_size = 4;//iio_device_get_sample_size(buffer->dev, buffer->mask);
 	buf_size = 4/*samples_count*/ * sample_size;

// 	for (i = 0; i < nb_blocks; i++) {
		stream->blocks[i] = iio_buffer_create_block(buffer, buf_size);
// 		err = iio_err(stream->blocks[i]);
// 		if (err) {
// 			stream->blocks[i] = NULL;
// 			goto err_free_stream_blocks;
// 		}
// 	}

	stream->buffer = buffer;
	stream->nb_blocks = nb_blocks;

	return stream;

// err_free_stream_blocks:
// 	for (i = 0; i < nb_blocks; i++)
// 		if (stream->blocks[i])
// 			iio_block_destroy(stream->blocks[i]);
// 	free(stream->blocks);
// err_free_stream:
// 	free(stream);
// 	return iio_ptr(err);
    return NULL; // Placeholder return value
}

size_t iio_device_get_sample_size(const struct iio_device *dev,
				   const struct iio_channels_mask *mask)
{
	size_t size = 0;
	unsigned int i, largest = 1;
	const struct iio_channel *prev = NULL;

	// if (mask->words != (dev->nb_channels + 31) / 32)
	// 	return -EINVAL;

	// for (i = 0; i < dev->nb_channels; i++) {
	// 	const struct iio_channel *chn = dev->channels[i];
	// 	unsigned int length = chn->format.length / 8 *
	// 		chn->format.repeat;

	// 	if (chn->index < 0)
	// 		break;
	// 	if (!iio_channels_mask_test_bit(mask, chn->number))
	// 		continue;

	// 	if (prev && chn->index == prev->index) {
	// 		prev = chn;
	// 		continue;
	// 	}

	// 	if (length > largest)
	// 		largest = length;

	// 	if (size % length)
	// 		size += 2 * length - (size % length);
	// 	else
	// 		size += length;

	// 	prev = chn;
	// }

	// if (size % largest)
	// 	size += largest - (size % largest);

	// return size;
    return 0; // Placeholder return value
}



static struct iio_task_token *
iio_task_do_enqueue(struct iio_task *task, void *elm, bool autoclear)
{
// 	struct iio_task_token *entry;
// 	int err;

// 	entry = iio_task_token_create(task, elm);
// 	if (iio_err(entry))
// 		return iio_err_cast(entry);

// 	err = iio_task_token_do_enqueue(task, entry, autoclear, true);
// 	if (err)
// 		goto err_destroy_entry;

// 	return entry;

// err_destroy_entry:
// 	iio_task_token_destroy(entry);
// 	return iio_ptr(err);
}

struct iio_task_token * iio_task_enqueue(struct iio_task *task, void *elm)
{
	return NULL;//(task, elm, false);
}

int iio_block_enqueue(struct iio_block *block, size_t bytes_used, bool cyclic)
{
	// struct iio_buffer *buffer = block->buffer;
	// const struct iio_device *dev = buffer->dev;
	// const struct iio_backend_ops *ops = dev->ctx->ops;

	// if (bytes_used > block->size)
	// 	return -EINVAL;

	// if (!bytes_used)
	// 	bytes_used = block->size;

	// if (ops->enqueue_block && block->pdata)
	// 	return ops->enqueue_block(block->pdata, bytes_used, cyclic);

	// if (block->token) {
	// 	/* Already enqueued */
	// 	return -EPERM;
	// }

	// block->bytes_used = bytes_used;
	// buffer->cyclic = cyclic;
	// block->token = iio_task_enqueue(buffer->worker, block);

	// return iio_err(block->token);
}

const struct iio_block *
iio_stream_get_next_block(struct iio_stream *stream)
{
	// const struct iio_device *dev = stream->buffer->dev;
	// bool is_tx = iio_device_is_tx(dev);
	// unsigned int i;
	// int err;

	// if (!stream->started) {
	// 	for (i = 1; !is_tx && i < stream->nb_blocks; i++) {
	// 		err = iio_block_enqueue(stream->blocks[i], 0, false);
	// 		if (err) {
	// 			dev_perror(dev, err, "Unable to enqueue block");
	// 			return iio_ptr(err);
	// 		}
	// 	}

	// 	stream->started = true;

	// 	if (is_tx)
	// 		return stream->blocks[0];

	// 	stream->all_enqueued = true;
	// }

	// err = iio_block_enqueue(stream->blocks[stream->curr], 0, false);
	// if (err < 0) {
	// 	dev_perror(dev, err, "Unable to enqueue block");
	// 	return iio_ptr(err);
	// }

	// if (!stream->buf_enabled) {
	// 	err = iio_buffer_enable(stream->buffer);
	// 	if (err) {
	// 		dev_perror(dev, err, "Unable to enable buffer");
	// 		return iio_ptr(err);
	// 	}

	// 	stream->buf_enabled = true;
	// }

	// stream->curr = (stream->curr + 1) % stream->nb_blocks;

	// stream->all_enqueued |= stream->curr == 0;
	// if (stream->all_enqueued) {
	// 	err = iio_block_dequeue(stream->blocks[stream->curr], false);
	// 	if (err < 0) {
	// 		dev_perror(dev, err, "Unable to dequeue block");
	// 		return iio_ptr(err);
	// 	}
	// }

	// return stream->blocks[stream->curr];
}


void stream(size_t rx_sample, size_t tx_sample, size_t block_size,
	    struct iio_stream *rxstream, struct iio_stream *txstream,
	    const struct iio_channel *rxchn, const struct iio_channel *txchn)
{
	const struct iio_device *dev;
	const struct iio_context *ctx;
	const struct iio_block *txblock, *rxblock;
	ssize_t nrx = 0;
	ssize_t ntx = 0;
	int err;

	//dev = iio_channel_get_device(rxchn);
	//ctx = iio_device_get_context(dev);

	while (1) /*(!stop)*/ {
		int16_t *p_dat, *p_end;
		ptrdiff_t p_inc;

		// rxblock = iio_stream_get_next_block(rxstream);
		// err = iio_err(rxblock);
		// if (err) {
		// 	ctx_perror(ctx, err, "Unable to receive block");
		// 	return;
		// }

		txblock = iio_stream_get_next_block(txstream);
		err = iio_err(txblock);
		if (err) {
			//ctx_perror(ctx, err, "Unable to send block");
			return;
		}

		/* READ: Get pointers to RX buf and read IQ from RX buf port 0 */
		p_inc = rx_sample;
		//p_end = iio_block_end(rxblock);
		// for (p_dat = iio_block_first(rxblock, rxchn); p_dat < p_end;
		//      p_dat += p_inc / sizeof(*p_dat)) {
		// 	/* Example: swap I and Q */
		// 	int16_t i = p_dat[0];
		// 	int16_t q = p_dat[1];

		// 	p_dat[0] = q;
		// 	p_dat[1] = i;
		// }

		/* WRITE: Get pointers to TX buf and write IQ to TX buf port 0 */
		// p_inc = tx_sample;
		// p_end = iio_block_end(txblock);
		// for (p_dat = iio_block_first(txblock, txchn); p_dat < p_end;
		//      p_dat += p_inc / sizeof(*p_dat)) {
		// 	p_dat[0] = 0; /* Real (I) */
		// 	p_dat[1] = 0; /* Imag (Q) */
		// }

		// nrx += block_size / rx_sample;
		// ntx += block_size / tx_sample;
		// ctx_info(ctx, "\tRX %8.2f MSmp, TX %8.2f MSmp\n", nrx / 1e6, ntx / 1e6);
	}
}