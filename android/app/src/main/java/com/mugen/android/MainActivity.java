package com.mugen.android;

import org.libsdl.app.SDLActivity;
import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends SDLActivity {
    private static final String TAG = "MUGEN";
    /* Bump this when bundled data changes: forces re-extraction on upgrade.
     * v0.1.2 bumped it so every device re-extracts once, guaranteeing a clean
     * payload regardless of what older builds left on storage. */
    private static final String DATA_MARKER = ".mugen_v3";
    /* Must exist after extraction, otherwise the payload is incomplete. */
    private static final String DATA_PROBE = "data/fight.def";

    private static volatile String sGameDir = null;
    private static int sExtractedFiles = 0;
    private static long sExtractedBytes = 0;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Extract bundled MUGEN data BEFORE SDL starts (blocking: SDL_main
        // must never race the extraction). Runs once; ~2s for 6 MB.
        extractGameData();
        super.onCreate(savedInstanceState);
        // The in-game folder picker browses /storage/emulated/0 so users can
        // load custom characters from any folder they choose. On legacy-storage
        // targets (targetSdk 28) that needs a runtime READ grant.
        requestStoragePermission();
    }

    private void requestStoragePermission() {
        try {
            if (Build.VERSION.SDK_INT >= 23) {
                if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                        != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(new String[]{
                            Manifest.permission.READ_EXTERNAL_STORAGE,
                            Manifest.permission.WRITE_EXTERNAL_STORAGE
                    }, 1001);
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "storage permission request failed", e);
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[]{ "game" };
    }

    /**
     * SDL_main receives this as argv[1], so the native side gets the exact
     * directory we extracted to — no storage-path guessing in C (internal vs
     * external fallback would otherwise mismatch SDL_AndroidGetExternalStoragePath).
     */
    @Override
    protected String[] getArguments() {
        String dir = sGameDir;
        return dir != null ? new String[]{ dir } : new String[0];
    }

    /**
     * Copy assets/mugen/** to <files>/mugen — the same folder layout as the PC
     * version. Users can drop custom characters into mugen/chars/ with any
     * file manager. Marker file + probe check guard against partial runs.
     */
    private void extractGameData() {
        if (sGameDir != null) return;
        try {
            File base = getExternalFilesDir(null);
            if (base == null) base = getFilesDir();
            File mugenDir = new File(base, "mugen");
            File marker = new File(base, DATA_MARKER);
            if (marker.isFile() && probeOk(mugenDir)) {
                Log.i(TAG, "game data already present (marker " + DATA_MARKER
                        + " ok, probe ok): " + mugenDir.getAbsolutePath());
                sGameDir = mugenDir.getAbsolutePath();
                return;
            }
            Log.i(TAG, "Extracting MUGEN data to " + mugenDir.getAbsolutePath());
            long t0 = System.currentTimeMillis();
            marker.delete();
            sExtractedFiles = 0;
            sExtractedBytes = 0;
            copyAssetDir("mugen", mugenDir);
            Log.i(TAG, "extraction stats: " + sExtractedFiles + " files, "
                    + (sExtractedBytes / 1024) + " KiB");
            if (!probeOk(mugenDir)) {
                throw new IllegalStateException(
                    "extraction incomplete: " + DATA_PROBE + " missing (assets broken?)");
            }
            marker.createNewFile();
            sGameDir = mugenDir.getAbsolutePath();
            Log.i(TAG, "Data extracted in " + (System.currentTimeMillis() - t0) + "ms");
        } catch (Exception e) {
            Log.e(TAG, "game data extraction failed", e);
            // sGameDir stays null -> native side shows a visible error screen
        }
    }

    private static boolean probeOk(File mugenDir) {
        return new File(mugenDir, DATA_PROBE).isFile();
    }

    private void copyAssetDir(String path, File dst) throws Exception {
        dst.mkdirs();
        String[] items = getAssets().list(path);
        if (items == null) return;
        for (String item : items) {
            String srcPath = path + "/" + item;
            File dstFile = new File(dst, item);
            String[] sub = getAssets().list(srcPath);
            if (sub != null && sub.length > 0) {
                copyAssetDir(srcPath, dstFile);
            } else {
                copyAssetFile(srcPath, dstFile);
            }
        }
    }

    /**
     * A childless asset entry can be a file OR an empty directory; getAssets()
     * cannot tell them apart, so try to open it and fall back to mkdirs().
     */
    private void copyAssetFile(String srcPath, File dstFile) throws Exception {
        InputStream in = null;
        OutputStream out = null;
        try {
            in = getAssets().open(srcPath);
            out = new FileOutputStream(dstFile);
            byte[] buf = new byte[65536];
            int n;
            long total = 0;
            while ((n = in.read(buf)) > 0) { out.write(buf, 0, n); total += n; }
            sExtractedFiles++;
            sExtractedBytes += total;
        } catch (java.io.FileNotFoundException e) {
            Log.w(TAG, "asset not a file (kept as dir): " + srcPath);
            dstFile.mkdirs(); // empty directory kept by aapt
        } finally {
            if (out != null) try { out.close(); } catch (Exception ignored) { }
            if (in != null) try { in.close(); } catch (Exception ignored) { }
        }
    }
}
