/*
 *
 */
#undef BOOTSTRAP
#include "config.h"
#include "libopenbios/bindings.h"
#include "arch/common/nvram.h"
#include "drivers/drivers.h"
#include "libc/diskio.h"
#include "libc/vsprintf.h"
#include "libopenbios/sys_info.h"
#include "openprom.h"
#include "boot.h"
#include "context.h"

uint32_t kernel_image;
uint32_t kernel_size;
uint32_t qemu_cmdline;
uint32_t cmdline_size;
char boot_device;
const void *romvec;

void go(void)
{
	ucell address, type, size;
	int image_retval = 0, intprop, proplen, target, device;
	phandle_t chosen;
	char *prop, *id, *name;
	static char bootpathbuf[128], bootargsbuf[128], buf[128];

	/* Get the entry point and the type (see forth/debugging/client.fs) */
	feval("saved-program-state >sps.entry @");
	address = POP();
	feval("saved-program-state >sps.file-type @");
	type = POP();
	feval("saved-program-state >sps.file-size @");
	size = POP();

	/* SPARC32 is slightly unusual in that before invoking any loaders, a romvec array
	   needs to be set up to pass certain parameters using a C struct. Hence this section
	   extracts the relevant boot information and places it in obp_arg. */
	
	/* Get the stdin and stdout paths */
	chosen = find_dev("/chosen");
	intprop = get_int_property(chosen, "stdin", &proplen);
	PUSH(intprop);
	fword("get-instance-path");
	((struct linux_romvec *)romvec)->pv_stdin = pop_fstr_copy();

	intprop = get_int_property(chosen, "stdout", &proplen);
	PUSH(intprop);
	fword("get-instance-path");
	((struct linux_romvec *)romvec)->pv_stdout = pop_fstr_copy();

	/* Get the name of the selected boot device, along with the device and unit number */
	prop = get_property(chosen, "bootpath", &proplen);
	strncpy(bootpathbuf, prop, proplen);
	prop = get_property(chosen, "bootargs", &proplen);
	strncpy(bootargsbuf, prop, proplen);	

	/* Set bootpath pointer used in romvec table to the bootpath */
        push_str(bootpathbuf);
        fword("pathres-resolve-aliases");
        bootpath = pop_fstr_copy();
        printk("bootpath: %s\n", bootpath);

	/* Now do some work to get hold of the target, partition etc. */
	push_str(bootpathbuf);
	feval("open-dev");
	feval("ihandle>boot-device-handle drop to my-self");
	push_str("name");
	fword("get-my-property");
	POP();
	name = pop_fstr_copy();

        if (!strncmp(name, "sd", 2)) {

		/*
		  Old-style SunOS disk paths are given in the form:

			sd(c,t,d):s

		  where:
		    c = controller (Nth controller in system, usually 0)
		    t = target (my-unit phys.hi)
		    d = device/LUN (my-unit phys.lo)
		    s = slice/partition (my-args)
		*/

		/* Controller currently always 0 */
		obp_arg.boot_dev_ctrl = 0;

		/* Get the target, device and slice */
		fword("my-unit");
		target = POP();
		device = POP();

		fword("my-args");
		id = pop_fstr_copy();

		if (id != NULL) {
			snprintf(buf, sizeof(buf), "sd(0,%d,%d):%c", target, device, id[0]);
			obp_arg.dev_partition = id[0] - 'a';
		} else {
			snprintf(buf, sizeof(buf), "sd(0,%d,%d)", target, device);
			obp_arg.dev_partition = 0;
		}

		obp_arg.boot_dev_unit = target;

		obp_arg.boot_dev[0] = buf[0];
		obp_arg.boot_dev[1] = buf[1];
		obp_arg.argv[0] = buf;
        	obp_arg.argv[1] = bootargsbuf;

        } else if (!strncmp(name, "SUNW,fdtwo", 10)) {
		
		obp_arg.boot_dev_ctrl = 0;
		obp_arg.boot_dev_unit = 0;
		obp_arg.dev_partition = 0;

		strcpy(buf, "fd()");

		obp_arg.boot_dev[0] = buf[0];
		obp_arg.boot_dev[1] = buf[1];
		obp_arg.argv[0] = buf;
        	obp_arg.argv[1] = bootargsbuf;

        } else if (!strncmp(name, "le", 2)) {

		obp_arg.boot_dev_ctrl = 0;
		obp_arg.boot_dev_unit = 0;
		obp_arg.dev_partition = 0;

		strcpy(buf, "le()");

		obp_arg.boot_dev[0] = buf[0];
		obp_arg.boot_dev[1] = buf[1];
		obp_arg.argv[0] = buf;
        	obp_arg.argv[1] = bootargsbuf;

	}
		
	printk("\nJumping to entry point " FMT_ucellx " for type " FMT_ucellx "...\n", address, type);

	switch (type) {
		case 0x0:
			/* Start ELF boot image */
			image_retval = start_elf((unsigned long)address,
                                                 (unsigned long)romvec);

			break;

		case 0x1:
			/* Start ELF image */
			image_retval = start_elf((unsigned long)address,
                                                 (unsigned long)romvec);

			break;

		case 0x5:
			/* Start a.out image */
			image_retval = start_elf((unsigned long)address,
                                                 (unsigned long)romvec);

			break;

		case 0x10:
			/* Start Fcode image */
			printk("Evaluating FCode...\n");
			PUSH(address);
			PUSH(1);
			fword("byte-load");
			image_retval = 0;
			break;

		case 0x11:
			/* Start Forth image */
			PUSH(address);
			PUSH(size);
			fword("eval2");
			image_retval = 0;
			break;
	}

	printk("Image returned with return value %#x\n", image_retval);
}


void boot(void)
{
	/* Boot preloaded kernel */
        if (kernel_size) {
            printk("[sparc] Kernel already loaded\n");
            start_elf(kernel_image, (unsigned long)romvec);
        }
}
