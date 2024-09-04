insmod kernel_driver/la9310rfnm/la9310rfnm.ko


insmod kernel_driver/la9310rfnm/rfnm_lalib.ko
insmod kernel_driver/la9310rfnm/rfnm_daughterboard.ko

insmod kernel_driver/la9310rfnm/rfnm_usb_function.ko
insmod kernel_driver/la9310rfnm/rfnm_usb.ko file=/rfnm/scripts/backing_storage
insmod kernel_driver/la9310rfnm/rfnm_usb_boost.ko file=/rfnm/scripts/backing_storage_boost

#insmod kernel_driver/la9310rfnm/rfnm_lime.ko
#insmod kernel_driver/la9310rfnm/rfnm_granita.ko
insmod kernel_driver/la9310rfnm/rfnm_breakout.ko