#include <ao/ao.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHUNKBYTES (44100 / 6)
#define HISTORYSIZE 32
#define PATHSIZE 8192

struct Sample
{
    char *bytes;
    uint32_t num_bytes;
};

struct Level
{
    struct Sample *samples;
    size_t num_samples;
};

int
filter_no_dots(const struct dirent *de)
{
    return strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0;
}

bool
load_sample(char *path, struct Sample *sample)
{
    FILE *fp = NULL;
    long filesize;

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "Could not open file '%s': ", path);
        perror(NULL);
        return false;
    }

    if (fseek(fp, 0, SEEK_END) == -1)
    {
        fprintf(stderr, "Could not seek to end of file '%s': ", path);
        perror(NULL);
        fclose(fp);
        return false;
    }

    if ((filesize = ftell(fp)) == -1)
    {
        fprintf(stderr, "Could not get file size of file '%s': ", path);
        perror(NULL);
        fclose(fp);
        return false;
    }
    sample->num_bytes = filesize;

    rewind(fp);

    sample->bytes = calloc(filesize, 1);
    if (sample->bytes == NULL)
    {
        fprintf(stderr, "Could not allocate buffer for file '%s': ", path);
        perror(NULL);
        fclose(fp);
        return false;
    }

    if (fread(sample->bytes, filesize, 1, fp) != 1)
    {
        fprintf(stderr, "Unexpected end of file, '%s'\n", path);
        fclose(fp);
        return false;
    }

    fclose(fp);
    return true;
}

bool
load_samples(struct Level *levels, size_t num_levels, struct Sample *bg)
{
    size_t i;
    struct dirent **namelist;
    int j, n;
    char path[PATHSIZE] = "";

    if (snprintf(path, PATHSIZE, "%s/background.raw", SAMPLE_DIR) >= PATHSIZE)
    {
        fprintf(stderr, "Building path for sample dir (bg) failed, truncated\n");
        return false;
    }

    fprintf(stderr, "[load] '%s'\n", path);
    if (!load_sample(path, bg))
        return false;

    for (i = 0; i < num_levels; i++)
    {
        if (snprintf(path, PATHSIZE, "%s/level%zu", SAMPLE_DIR, i) >= PATHSIZE)
        {
            fprintf(stderr, "Building path for sample dir failed, truncated\n");
            return false;
        }

        n = scandir(path, &namelist, filter_no_dots, alphasort);
        if (n < 0)
        {
            fprintf(stderr, "Could not read sample dir '%s': ", path);
            perror(NULL);
            return false;
        }

        levels[i].num_samples = n;
        levels[i].samples = calloc(levels[i].num_samples, sizeof (struct Sample));
        if (levels[i].samples == NULL)
        {
            fprintf(stderr, "Could not calloc for .samples on %zu: ", i);
            perror(NULL);
            return false;
        }

        for (j = 0; j < n; j++)
        {
            if (snprintf(path, PATHSIZE, "%s/level%zu/%s", SAMPLE_DIR, i,
                         namelist[j]->d_name) >= PATHSIZE)
            {
                fprintf(stderr, "Building path for sample failed, truncated\n");
                return false;
            }
            free(namelist[j]);

            fprintf(stderr, "[load] '%s'\n", path);
            if (!load_sample(path, &levels[i].samples[j]))
                return false;
        }
    }

    return true;
}

double
disk_activity_level(void)
{
    FILE *fp;
    char *line = NULL;
    size_t len = 0, i;
    ssize_t read;
    int r, reads, writes;
    uint64_t now, max = 0, delta = 0;
    double activity;

    static uint64_t deltas[HISTORYSIZE] = {0}, prev;
    static size_t deltas_fill = 0;

    fp = fopen("/proc/diskstats", "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Could not open /proc/diskstats: ");
        perror(NULL);
        return -1;
    }

    now = 0;
    while ((read = getline(&line, &len, fp)) != -1)
    {
        r = sscanf(line, "%*d %*d %*s %d %*d %*d %*d %d %*d %*d %*d %*d %*d %*d\n",
                   &reads, &writes);
        if (r == 2)
        {
            now += reads + writes;
        }
    }

    activity = 0;
    if (prev > 0)
    {
        /* See how many operations there have been since we last
         * checked. This is an absolute value. We don't know if this is
         * a high value or a low value, because this depends a lot on
         * your machine. To get same ("fake") feeling about the meaning
         * of those numbers, store the last 'HISTORYSIZE' deltas. We
         * then compute 'activity' by comparing the current delta with
         * the highest delta we've seen. */
        delta = now - prev;
        if (deltas_fill == HISTORYSIZE - 1)
        {
            for (i = 0; i < HISTORYSIZE - 1; i++)
                deltas[i] = deltas[i + 1];
            deltas[HISTORYSIZE - 1] = delta;
        }
        else
        {
            deltas[deltas_fill] = delta;
            deltas_fill++;
        }

        max = 0;
        for (i = 0; i < deltas_fill; i++)
            max = deltas[i] > max ? deltas[i] : max;

        if (max != 0)
            activity = (double)delta / max;
    }

    fprintf(stderr, "\033[K[activity] %.2f%%, delta %zu, max %zu\r",
            (activity > 1 ? 1 : activity) * 100, delta, max);

    prev = now;

    free(line);
    fclose(fp);

    return activity;
}

int
main()
{
    int default_driver, randint;
    ao_device *device;
    ao_sample_format format = {0};
    struct Level levels[3] = {0};
    struct Sample bg = {0};
    uint32_t bg_at = 0, chunk, remaining;
    double activity;
    int level;

    if (!load_samples(levels, sizeof levels / sizeof levels[0], &bg))
    {
        fprintf(stderr, "Could not load samples, aborting\n");
        return 1;
    }

    srand(time(NULL));

    ao_initialize();
    default_driver = ao_default_driver_id();
    format.bits = 16;
    format.channels = 1;
    format.rate = 44100;
    format.byte_format = AO_FMT_LITTLE;
    device = ao_open_live(default_driver, &format, NULL);
    if (device == NULL)
    {
        fprintf(stderr, "Could not open sound device, aborting\n");
        return 1;
    }

    for (;;)
    {
        activity = disk_activity_level();
        if (activity < 0)
        {
            fprintf(stderr, "Error reading disk activity\n");
            return 1;
        }

        /* If 'activity' is above certain thresholds, we play samples of
         * that level. Level 0 is mild activity, level 1 is some
         * activity and level 2 is heavy activity. */
        if (activity > 0.4)
            level = 2;
        else if (activity > 0.1)
            level = 1;
        else if (activity > 0)
            level = 0;
        else
            level = -1;

        if (level >= 0)
        {
            randint = rand() % levels[level].num_samples;
            ao_play(device, levels[level].samples[randint].bytes,
                    levels[level].samples[randint].num_bytes);
        }
        else
        {
            /* If there's no disk activity right now, we play a part of
             * a somewhat longer noise sample. We don't play the whole
             * thing to have a quick response time. */
            remaining = bg.num_bytes - bg_at;
            if (remaining > CHUNKBYTES)
                chunk = CHUNKBYTES;
            else
                chunk = remaining;
            ao_play(device, bg.bytes + bg_at, chunk);
            bg_at += chunk;
            bg_at %= bg.num_bytes;
        }
    }

    /* unreached */
}
