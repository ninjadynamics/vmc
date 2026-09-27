#ifndef PS2_VMC_FORMAT_H
#define PS2_VMC_FORMAT_H

/* Writes a new formatted 8 MB card image with ECC at path (created or
   truncated); 0 or a negative iomanX error. The caller checks it is absent. */
int vmc_format_image(const char *path);

#endif
