Wireshark E-DIO24 Protocol Decoder
==================================

This is the decoder of [E-DIO24](https://www.mccdaq.com/ethernet-data-acquisition/24-channel-digital-io-daq/E-DIO24-Series) protocol implemented with the [wireshark Generic Dissector](http://wsgd.free.fr/).

Install
-------

Download generic.so from the [Download Page](http://wsgd.free.fr/download.html)

Find your "Personal configuration" in the Wireshark menu "Help -- About -- Folders".

Put the files in the "Personal configuration" folder:

    plugins/generic.so
    profiles/edio24.wsgd
    profiles/edio24.fdesc

And then you can open the capture file in the wireshark.


