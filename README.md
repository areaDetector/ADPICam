ADPICam
=============
An 
[EPICS](http://www.aps.anl.gov/epics/) 
[areaDetector](https://github.com/areaDetector/areaDetector/blob/master/README.md) 
driver for Cameras from Princeton Instruments that support the PICAM library.  
[PICam](ftp://ftp.princetoninstruments.com/public/Manuals/Princeton%20Instruments/PICam%20User%20Manual.pdf)
This driver is based on the PICAM Virtual Camera Library.  

I have built (but not tested) it using Centos 8 x86_64.
This requires the SDK installed, it is available from Princetond FTP site.

It runs on Microsoft Windows 7 and above, and supports 32- and 64-bit use.
Detectors supported by the vendor PICAM driver library include:
* PI-MAX3
* PI-MAX4, PI-MAX4:RF, PI-MAX4:EM
* PIoNIR/NIRvana
* NIRvana-LN
* PIXIS, PIXIS-XB, PIXIS-XF, PIXIS-XO
* ProEM
* ProEM+
* PyLoN
* PyLoN-IR
* Quad-RO

This detector has been tested with ProEM and Quad-RO cameras and to some degree with the 
PIXIS Demo camera (soft camera in the library).  Most notably missing from the 
library so far are the Pulse and Modulation Parameters used mostly in the 
PI-MAX3 & 4 cameras.     

Additional information:
* [Documentation](https://cars.uchicago.edu/software/epics/PICamDoc.html).
* [Release notes and links to source and binary releases](RELEASE.md).
