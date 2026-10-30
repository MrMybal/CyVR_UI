// SPDX-License-Identifier: MIT
package com.cyvr.ui;

import android.app.NativeActivity;
import android.app.PendingIntent;
import android.content.Intent;

/** Minimal lifecycle bridge. Rendering and application UI stay in shared C++. */
public class PanelActivity extends NativeActivity {
    public int presentationMode() { return 0; }
    public int initialPage() { return getIntent().getIntExtra("cyvr.page", 0); }
    public int immersiveFailed() { return getIntent().getBooleanExtra("cyvr.immersive_failed", false) ? 1 : 0; }

    public void launchPresentation(final int mode, final int page) {
        launchPresentation(mode, page, false);
    }

    public void launchPresentation(final int mode, final int page, final boolean immersiveFailed) {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                if (mode == 1) {
                    Intent immersive = new Intent().setClassName(PanelActivity.this, getPackageName() + ".ImmersiveActivity")
                        .setAction(Intent.ACTION_MAIN).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        .putExtra("cyvr.page", page);
                    startActivity(immersive);
                    finishAndRemoveTask();
                } else {
                    Intent panel = new Intent(PanelActivity.this, PanelActivity.class).setAction(Intent.ACTION_MAIN)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK).putExtra("cyvr.page", page)
                        .putExtra("cyvr.immersive_failed", immersiveFailed);
                    PendingIntent pending = PendingIntent.getActivity(PanelActivity.this, 0, panel,
                        PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
                    Intent home = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        .putExtra("extra_launch_in_home_pending_intent", pending);
                    startActivity(home);
                    finishAndRemoveTask();
                }
            }
        });
    }
}
