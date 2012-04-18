/*
 * Copyright 2012, Havlena Petr <havlenapetr@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.settings.device;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.Context;
import android.content.res.Resources;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceManager;

public class UsbOtgSwitch implements OnPreferenceChangeListener {

    private static final String FILE = "/sys/bus/i2c/drivers/fsa9480/7-0025/mode";

    private static final String FSA9480_MODE_USB = "USB\n";
    private static final String FSA9480_MODE_OTG = "OTG\n";

    private static final int NOTIFICATION_ID = 1;

    private Context mContext;

    public UsbOtgSwitch(Context ctx) {
        mContext = ctx;
    }

    public static boolean isSupported() {
        return Utils.fileExists(FILE);
    }

    public boolean isEnabled() {
        String value = Utils.readValue(FILE);
        return value.equals(FSA9480_MODE_OTG);
    }

    private boolean isDisabled() {
        String value = Utils.readValue(FILE);
        return value.equals(FSA9480_MODE_USB);
    }

    private boolean showNotification(boolean enabled) {
        NotificationManager nm = (NotificationManager) mContext
                .getSystemService(Context.NOTIFICATION_SERVICE);

        if (nm == null) {
            return false;
        }

        // cancel previous notification
        nm.cancel(NOTIFICATION_ID);

        if(!enabled) {
            return false;
        }

        Resources r = mContext.getResources();
        CharSequence title = r.getText(R.string.usb_otg_notifi_title);
        CharSequence message = r.getText(R.string.usb_otg_notifi_msg);

        Notification n = new Notification();
        n.when = 0;
        n.defaults &= ~Notification.DEFAULT_SOUND;
        n.flags = Notification.FLAG_ONGOING_EVENT;
        n.tickerText = title;

        Intent intent = new Intent();
        intent.setClass(mContext, com.android.settings.device.DeviceSettings.class);
        PendingIntent pi = PendingIntent.getActivity(mContext, 0, intent, 0);

        n.icon = com.android.internal.R.drawable.stat_sys_data_usb;
        n.setLatestEventInfo(mContext, title, message, pi);

        nm.notify(NOTIFICATION_ID, n);

        return true;
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        Boolean enable = (Boolean) newValue;
        Utils.writeValue(FILE, enable ? FSA9480_MODE_OTG : FSA9480_MODE_USB);

        boolean enabled = isEnabled();
        showNotification(enabled);

        return enable ? enabled : !enabled;
    }

}
