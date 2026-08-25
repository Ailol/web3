static int cloud_put(
    qck_dev_provider_t provider,
    const char *path,
    const void *data,
    size_t len,
    void *ctx)
{
    switch (provider)
    {
    case QCK_DEV_GDRIVE:
        return google_drive_upload(ctx, path, data, len);

    case QCK_DEV_ONEDRIVE:
        return onedrive_upload(ctx, path, data, len);

    default:
        return -1;
    }
}