#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static struct proc_dir_entry *entry;

static u8 *test_kasan_buffer;

static ssize_t test_kasan_write(
      struct file *filep, const char __user *buf,
      size_t length, loff_t *off)
{
    unsigned long n;
    n = copy_from_user(test_kasan_buffer, buf, length);
    return length;
}

static struct proc_ops  test_kasan_fops = {
    //.owner = THIS_MODULE,
    .proc_write = test_kasan_write
};

static int __init test_kasan_init(void)
{
    test_kasan_buffer = kmalloc(14, GFP_KERNEL);
    if (!test_kasan_buffer)
        return -ENOMEM;

    entry = proc_create("test_kasan", S_IWUSR, NULL, &test_kasan_fops);
    if (!entry)
        return -ENOMEM;

    return 0;
}

static void test_kasan_exit(void)
{
    kfree(test_kasan_buffer);
    remove_proc_entry("test_kasan", NULL);
}

MODULE_AUTHOR("Star Lab <info@starlab.io>");
MODULE_DESCRIPTION("Testing KASAN");
MODULE_LICENSE("GPL");

module_init(test_kasan_init);
module_exit(test_kasan_exit);