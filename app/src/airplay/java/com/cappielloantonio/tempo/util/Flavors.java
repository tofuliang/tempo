package com.cappielloantonio.tempo.util;

import android.content.Context;

import com.google.android.gms.cast.framework.CastContext;
import com.google.android.gms.common.ConnectionResult;
import com.google.android.gms.common.GoogleApiAvailability;

public class Flavors {
    public static final String FLAVOR_NAME = "airplay";
    public static final boolean SUPPORTS_AIRPLAY = true;

    public static void initializeCastContext(Context context) {
        if (GoogleApiAvailability.getInstance().isGooglePlayServicesAvailable(context) == ConnectionResult.SUCCESS)
            CastContext.getSharedInstance(context);
    }
}
