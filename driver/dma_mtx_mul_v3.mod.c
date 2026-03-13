#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xcb8b6ec6, "kfree" },
	{ 0x7a5ffe84, "init_wait_entry" },
	{ 0xd272d446, "schedule" },
	{ 0x0db8d68d, "prepare_to_wait_event" },
	{ 0xc87f4bab, "finish_wait" },
	{ 0x9b1de7cb, "_dev_warn" },
	{ 0xd272d446, "__stack_chk_fail" },
	{ 0xe4de56b4, "__ubsan_handle_load_invalid_value" },
	{ 0x3e604df7, "__vma_start_write" },
	{ 0x8e3e02f2, "dma_mmap_attrs" },
	{ 0x16ab4215, "__wake_up" },
	{ 0x0571dc46, "kthread_stop" },
	{ 0x1595e410, "device_destroy" },
	{ 0xa1dacb42, "class_destroy" },
	{ 0x4e54d6ac, "cdev_del" },
	{ 0x0bc5fb0d, "unregister_chrdev_region" },
	{ 0xe8213e80, "_printk" },
	{ 0xc1e6c71e, "__mutex_init" },
	{ 0x5403c125, "__init_waitqueue_head" },
	{ 0x9f222e1e, "alloc_chrdev_region" },
	{ 0xd5f66efd, "cdev_init" },
	{ 0x8ea73856, "cdev_add" },
	{ 0x653aa194, "class_create" },
	{ 0xe486c4b7, "device_create" },
	{ 0x9ef1423b, "dma_set_mask" },
	{ 0x9ef1423b, "dma_set_coherent_mask" },
	{ 0x7f79e79a, "kthread_create_on_node" },
	{ 0x630dad60, "wake_up_process" },
	{ 0xbd03ed67, "random_kmalloc_seed" },
	{ 0xfaabfe5e, "kmalloc_caches" },
	{ 0xc064623f, "__kmalloc_cache_noprof" },
	{ 0x2719b9fa, "const_current_task" },
	{ 0x092a35a2, "_copy_from_user" },
	{ 0x092a35a2, "_copy_to_user" },
	{ 0xa53f4e29, "memcpy" },
	{ 0x7039d3ca, "dma_alloc_attrs" },
	{ 0xe54e0a6b, "__fortify_panic" },
	{ 0x5a844b26, "__x86_indirect_thunk_rax" },
	{ 0x5e505530, "kthread_should_stop" },
	{ 0x67628f51, "msleep" },
	{ 0x23ef80fb, "noop_llseek" },
	{ 0xd272d446, "__fentry__" },
	{ 0xd272d446, "__x86_return_thunk" },
	{ 0x90a48d82, "__ubsan_handle_out_of_bounds" },
	{ 0xbd03ed67, "__ref_stack_chk_guard" },
	{ 0xf46d5bf3, "mutex_lock" },
	{ 0xf46d5bf3, "mutex_unlock" },
	{ 0x7851be11, "__SCT__might_resched" },
	{ 0x91d6d561, "dma_free_attrs" },
	{ 0x9b1de7cb, "_dev_info" },
	{ 0xbebe66ff, "module_layout" },
};

static const u32 ____version_ext_crcs[]
__used __section("__version_ext_crcs") = {
	0xcb8b6ec6,
	0x7a5ffe84,
	0xd272d446,
	0x0db8d68d,
	0xc87f4bab,
	0x9b1de7cb,
	0xd272d446,
	0xe4de56b4,
	0x3e604df7,
	0x8e3e02f2,
	0x16ab4215,
	0x0571dc46,
	0x1595e410,
	0xa1dacb42,
	0x4e54d6ac,
	0x0bc5fb0d,
	0xe8213e80,
	0xc1e6c71e,
	0x5403c125,
	0x9f222e1e,
	0xd5f66efd,
	0x8ea73856,
	0x653aa194,
	0xe486c4b7,
	0x9ef1423b,
	0x9ef1423b,
	0x7f79e79a,
	0x630dad60,
	0xbd03ed67,
	0xfaabfe5e,
	0xc064623f,
	0x2719b9fa,
	0x092a35a2,
	0x092a35a2,
	0xa53f4e29,
	0x7039d3ca,
	0xe54e0a6b,
	0x5a844b26,
	0x5e505530,
	0x67628f51,
	0x23ef80fb,
	0xd272d446,
	0xd272d446,
	0x90a48d82,
	0xbd03ed67,
	0xf46d5bf3,
	0xf46d5bf3,
	0x7851be11,
	0x91d6d561,
	0x9b1de7cb,
	0xbebe66ff,
};
static const char ____version_ext_names[]
__used __section("__version_ext_names") =
	"kfree\0"
	"init_wait_entry\0"
	"schedule\0"
	"prepare_to_wait_event\0"
	"finish_wait\0"
	"_dev_warn\0"
	"__stack_chk_fail\0"
	"__ubsan_handle_load_invalid_value\0"
	"__vma_start_write\0"
	"dma_mmap_attrs\0"
	"__wake_up\0"
	"kthread_stop\0"
	"device_destroy\0"
	"class_destroy\0"
	"cdev_del\0"
	"unregister_chrdev_region\0"
	"_printk\0"
	"__mutex_init\0"
	"__init_waitqueue_head\0"
	"alloc_chrdev_region\0"
	"cdev_init\0"
	"cdev_add\0"
	"class_create\0"
	"device_create\0"
	"dma_set_mask\0"
	"dma_set_coherent_mask\0"
	"kthread_create_on_node\0"
	"wake_up_process\0"
	"random_kmalloc_seed\0"
	"kmalloc_caches\0"
	"__kmalloc_cache_noprof\0"
	"const_current_task\0"
	"_copy_from_user\0"
	"_copy_to_user\0"
	"memcpy\0"
	"dma_alloc_attrs\0"
	"__fortify_panic\0"
	"__x86_indirect_thunk_rax\0"
	"kthread_should_stop\0"
	"msleep\0"
	"noop_llseek\0"
	"__fentry__\0"
	"__x86_return_thunk\0"
	"__ubsan_handle_out_of_bounds\0"
	"__ref_stack_chk_guard\0"
	"mutex_lock\0"
	"mutex_unlock\0"
	"__SCT__might_resched\0"
	"dma_free_attrs\0"
	"_dev_info\0"
	"module_layout\0"
;

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "9EFB573FA93FF603C5C25D6");
