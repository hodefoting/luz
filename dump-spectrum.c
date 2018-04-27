#include <stdio.h>
#include <stdint.h>
#include <babl/babl.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "luz.h"

#define WIDTH  512
#define HEIGHT 128

void constrain_rgb (float *rgb)
{
  float w;
  w = (0 < rgb[0]) ? 0 : rgb[0];
  w = (w < rgb[1]) ? w : rgb[1];
  w = (w < rgb[2]) ? w : rgb[2];
  w = -w;

  /* Add just enough white to make r, g, b all positive. */

  if (w > 0) {
    rgb[0] += w;
    rgb[1] += w;
    rgb[2] += w;
  }

  for (int c = 0; c < 3; c++)
    if (rgb[c] > 1)
      rgb[c] = 1;
}

int main (int argc, char **argv)
{
  uint8_t *img = malloc (WIDTH * HEIGHT *3);
  babl_init ();
  Luz *luz = luz_new ("");
  float maxband = 0;
  Spectrum spec = luz_parse_spectrum (luz, argv[1]?argv[1]:"observer_x");//{{0,0,0,0,0,0,0,0,0,0,0}};
  for (int b = 0; b < WIDTH; b++)
  {
    Spectrum rspec;
    int y;
    float rgb[3];
    float max = 1.0;
    memset (&rspec, 0, sizeof(spec));
    rspec.bands[(int) (b * 1.0 / WIDTH * LUZ_SPECTRUM_BANDS)] = LUZ_SPECTRUM_BANDS / 2;
    luz_spectrum_to_rgb (luz, &rspec, &rgb[0]);

    constrain_rgb(rgb);

    for (y = 0; y < HEIGHT; y++)
      {
        img[(y * WIDTH + b) * 3 + 0] = 0.66 * rgb[0] * 255 / max;
        img[(y * WIDTH + b) * 3 + 1] = 0.66 * rgb[1] * 255 / max;
        img[(y * WIDTH + b) * 3 + 2] = 0.66 * rgb[2] * 255 / max;
      }
  }

  for (int b = 0; b < LUZ_SPECTRUM_BANDS; b++)
  {
    float val = spec.bands[b];
    if (val > maxband) maxband = val;
  }

  for (int x = 0; x < WIDTH; x++)
  {
    int b = x * 1.0 / WIDTH * LUZ_SPECTRUM_BANDS;
    int y = HEIGHT-1-spec.bands[b] /maxband * HEIGHT;
    if (y<0) y = 0;
    if (y>HEIGHT-1) y = HEIGHT-1;
    while (y >=0)
    {
      img[(y * WIDTH + x) * 3 + 0] = 0;
      img[(y * WIDTH + x) * 3 + 1] = 0;
      img[(y * WIDTH + x) * 3 + 2] = 0;
      y--;
    }
  }

  stbi_write_png ("spectrum.png", WIDTH, HEIGHT, 3, img, WIDTH * 3);

  return 0;
}
