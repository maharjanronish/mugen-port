package com.mugen.android;

import org.libsdl.app.SDLActivity;
import android.os.Bundle;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends SDLActivity {
    private static final String TAG = "MUGEN";
    private static boolean sGameDataReady = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Extract bundled MUGEN data on first run (or when version bumps)
        extractGameData();
        super.onCreate(savedInstanceState);
    }

    /**
     * Copy assets/mugen/** to <external files>/mugen — the same folder layout
     * as the PC version. Users can drop custom characters into mugen/chars/
     * with any file manager.
     */
    private void extractGameData() {
        if (sGameDataReady) return;
        try {
            File base = getExternalFilesDir(null);
            if (base == null) base = getFilesDir();
            File mugenDir = new File(base, "mugen");
            File marker = new File(base, ".mugen_v1");
            if (marker.exists() && mugenDir.isDirectory()) {
                sGameDataReady = true;
                return;
            }
            Log.i(TAG, "Extracting MUGEN data to " + mugenDir.getAbsolutePath());
            long t0 = System.currentTimeMillis();
            copyAssetDir("mugen", mugenDir);
            marker.createNewFile();
            sGameDataReady = true;
            Log.i(TAG, "Data extracted in " + (System.currentTimeMillis() - t0) + "ms");
        } catch (Exception e) {
            Log.e(TAG, "extract failed", e);
        }
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
                InputStream in = getAssets().open(srcPath);
                OutputStream out = new FileOutputStream(dstFile);
                byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                out.close();
                in.close();
            }
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[]{ "game" };
    }
}
