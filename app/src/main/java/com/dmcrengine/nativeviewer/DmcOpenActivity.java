package com.dmcrengine.nativeviewer;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/**
 * Dedicated real Activity entry point for external file opens.
 *
 * Samsung My Files has historically behaved differently for custom extensions
 * than the stock Android resolver.  This activity intentionally exists as a
 * real exported Activity (not an activity-alias) so OEM resolvers that ignore
 * aliases can still target a concrete component.
 */
public final class DmcOpenActivity extends Activity {
    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        forward(getIntent());
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        forward(intent);
    }

    private void forward(Intent incoming) {
        Intent forwarded = incoming == null ? new Intent() : new Intent(incoming);
        forwarded.setClass(this, MainActivity.class);
        // Preserve URI grants, ClipData, action, type, data, categories and flags.
        startActivity(forwarded);
        finish();
    }
}
