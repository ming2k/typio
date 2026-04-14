#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    const char *font_desc = "Sans 11";
    char family[128] = "sans-serif";
    float size = 16.0f;
    if (font_desc && font_desc[0]) {
        const char *last_space = strrchr(font_desc, ' ');
        if (last_space) {
            size = (float)atof(last_space + 1);
            size_t fam_len = (size_t)(last_space - font_desc);
            if (fam_len > sizeof(family) - 1) fam_len = sizeof(family) - 1;
            memcpy(family, font_desc, fam_len);
            family[fam_len] = '\0';
        } else {
            size = (float)atof(font_desc);
            if (size <= 0) {
                snprintf(family, sizeof(family), "%s", font_desc);
                size = 16.0f;
            }
        }
    }
    printf("Parsed: family='%s', size=%.2f\n", family, size);
    return 0;
}
