﻿using System;
using System.IO;

namespace OmApiNet
{
    public class OmDevice : OmSource
    {
        protected Om om;

        public bool validData;
        private bool hasChanged;
        public DateTime lastBatteryUpdate = DateTime.MinValue;

        public OmDevice(Om om, ushort deviceId)
        {
            this.om = om;
            this.deviceId = deviceId;
            validData = false;
        }

        // Properties
        public override SourceCategory Category
        { 
            get 
            {
                SourceCategory category = SourceCategory.Other;

                if (!Connected)
                {
                    category = SourceCategory.Removed;        // Device not attached
                }
                else
                {
                    if (IsDownloading)
                    {
                        category = SourceCategory.Downloading;        // Device attached, downloading
                    }
                    else
                    {
                        if (HasData)
                        {
                            if (HasNewData)
                            {
                                category = SourceCategory.NewData;        // Device attached, not downloading, non-empty data file, archive attribute set
// TODO: Once API supports data detection (non-empty & modified), remove this line
category = SourceCategory.Other;
                            }
                            else
                            {
                                category = SourceCategory.Downloaded;     // Device attached, not downloading, non-empty data file, archive attribute cleared
                            }
                        }
                        else
                        {
                            if (SessionId == 0)
                            {
                                if (BatteryLevel < 100)
                                {
                                    category = SourceCategory.Charging;       // Device attached, not downloading, empty data file, zero session-id, charging
                                }
                                else
                                {
                                    category = SourceCategory.Standby;        // Device attached, not downloading, empty data file, zero session-id, charged
                                }
                            }
                            else
                            {
                                category = SourceCategory.Outbox;         // Device attached, not downloading, empty data file, non-zero session id
                            }
                        }
                    }
                }

                return category; 
            } 
        }

        protected ushort deviceId;
        public override ushort DeviceId { get { return deviceId; } }

        protected uint sessionId = uint.MaxValue;
        public override uint SessionId { get { return sessionId; } }

        protected int firmwareVersion = int.MinValue;
        public int FirmwareVersion { get { return firmwareVersion; } }

        protected int hardwareVersion = int.MinValue;
        public int HardwareVersion { get { return hardwareVersion; } }

        protected OmApi.OM_LED_STATE ledColor = OmApi.OM_LED_STATE.OM_LED_UNKNOWN;
        public OmApi.OM_LED_STATE LedColor { get { return ledColor; } }

        protected int batteryLevel = int.MinValue;
        public int BatteryLevel { get { return batteryLevel; } }

        protected TimeSpan timeDifference = TimeSpan.MinValue;
        public TimeSpan TimeDifference { get { return timeDifference; } }

        protected DateTime startTime = DateTime.MinValue;
        public DateTime StartTime { get { return startTime; } }

        protected DateTime stopTime = DateTime.MinValue;
        public DateTime StopTime { get { return stopTime; } }

        protected OmApi.OM_DOWNLOAD_STATUS downloadStatus = OmApi.OM_DOWNLOAD_STATUS.OM_DOWNLOAD_NONE;
        public OmApi.OM_DOWNLOAD_STATUS DownloadStatus { get { return downloadStatus; } }

        protected int downloadValue = 0;
        public int DownloadValue { get { return downloadValue; } }


// TODO: Implement the required API for these (remember to clear archive on successful download)
public bool HasData { get { return true; } }
public bool HasNewData { get { return true; } }


        public bool IsDownloading
        {
            get { return Connected && DownloadStatus == OmApi.OM_DOWNLOAD_STATUS.OM_DOWNLOAD_PROGRESS; }
        }

        private bool connected;
        public bool Connected { get { return connected; } }
        internal void SetConnected(bool value)
        {
            if (connected != value) 
            { 
                connected = value; 
                validData = false;
                ledColor = OmApi.OM_LED_STATE.OM_LED_UNKNOWN;
                downloadStatus = OmApi.OM_DOWNLOAD_STATUS.OM_DOWNLOAD_NONE;
                downloadValue = 0;
            } 
        }


        public bool Update()
        {
            bool changed = false;
            DateTime now = DateTime.Now;

            if (!validData)
            {
                bool error = false;
                int res;

                // TODO: Error checking
                res = OmApi.OmGetVersion(deviceId, out firmwareVersion, out hardwareVersion);
                error |= OmApi.OM_FAILED(res);
                 
                uint time;
                res = OmApi.OmGetTime(deviceId, out time);
                error |= OmApi.OM_FAILED(res);
                timeDifference = (OmApi.OmDateTimeUnpack(time) - now);

                uint startTimeValue, stopTimeValue;
                res = OmApi.OmGetDelays(deviceId, out startTimeValue, out stopTimeValue);
                error |= OmApi.OM_FAILED(res);
                startTime = OmApi.OmDateTimeUnpack(startTimeValue);
                stopTime = OmApi.OmDateTimeUnpack(stopTimeValue);

                res = OmApi.OmGetSessionId(deviceId, out sessionId);
                error |= OmApi.OM_FAILED(res);

                changed = true;
                if (!error) { validData = true; }
            }

            if (lastBatteryUpdate == DateTime.MinValue || (now - lastBatteryUpdate) > TimeSpan.FromSeconds(60.0f))
            {
                int newBatteryLevel = OmApi.OmGetBatteryLevel(deviceId);
                lastBatteryUpdate = now;

                if (newBatteryLevel != batteryLevel)
                {
                    batteryLevel = newBatteryLevel;
                    changed = true;
                }
            }

            changed |= hasChanged; 
            hasChanged = false;

            return changed;
        }

        public bool SetLed(OmApi.OM_LED_STATE state)
        {
            ledColor = state;
            if (OmApi.OM_FAILED(OmApi.OmSetLed(deviceId, (int)ledColor)))
            {
                return false;
            }
            hasChanged = true;
            om.OnChanged(new OmDeviceEventArgs(this));
            return true;
        }

        public bool SyncTime()
        {
            DateTime now = DateTime.Now;
            if (OmApi.OM_FAILED(OmApi.OmSetTime(deviceId, OmApi.OmDateTimePack(now))))
            {
                return false;
            }
            timeDifference = TimeSpan.Zero;
            hasChanged = true;
            om.OnChanged(new OmDeviceEventArgs(this));
            return true;
        }

        public void UpdateDownloadStatus(OmApi.OM_DOWNLOAD_STATUS status, int value)
        {
            downloadStatus = status;
            downloadValue = value;
            hasChanged = true;
            om.OnChanged(new OmDeviceEventArgs(this, status));
        }

        string downloadFilename = null;
        string downloadFilenameRename = null;
        public void BeginDownloading(string filename, string renameFilename)
        {
            downloadFilename = filename;
            downloadFilenameRename = renameFilename;
            OmApi.OmBeginDownloading(deviceId, 0, -1, filename);
        }

        public void FinishedDownloading()
        {
            if (downloadFilename != null)
            {
                if (downloadFilenameRename != null && downloadFilename != downloadFilenameRename)
                {
                    File.Move(downloadFilename, downloadFilenameRename);
                }
            }
        }

        public void CancelDownload()
        {
            OmApi.OmCancelDownload(deviceId);
        }

        public bool SetInterval(DateTime start, DateTime stop)
        {
            bool failed = false;
            this.startTime = start;
            this.stopTime = stop;
            failed |= OmApi.OM_FAILED(OmApi.OmSetDelays(deviceId, OmApi.OmDateTimePack(start), OmApi.OmDateTimePack(stop)));
            failed |= OmApi.OM_FAILED(OmApi.OmCommit(deviceId));
validData = false;
            hasChanged = true;
            om.OnChanged(new OmDeviceEventArgs(this));
            return !failed;
        }

        public bool NeverRecord()
        {
            return SetInterval(DateTime.MaxValue, DateTime.MaxValue);
        }

        public bool AlwaysRecord()
        {
            return SetInterval(DateTime.MinValue, DateTime.MaxValue);
        }

        public void SetSessionId(uint sessionId)
        {
            OmApi.OmSetSessionId(deviceId, sessionId);
            OmApi.OmCommit(deviceId);
            validData = false;
            hasChanged = true;
            om.OnChanged(new OmDeviceEventArgs(this));
        }


        public bool Clear()
        {
            bool failed = false;


            failed |= OmApi.OM_FAILED(OmApi.OmCommit(deviceId));
            failed |= OmApi.OM_FAILED(OmApi.OmCommit(deviceId));

            OmApi.OmSetSessionId(deviceId, 0);                                                      // Clear the session id
            OmApi.OmSetMetadata(deviceId, "", 0);                                                   // No metadata
            OmApi.OmSetDelays(deviceId, OmApi.OM_DATETIME_INFINITE, OmApi.OM_DATETIME_INFINITE);    // Never log
            OmApi.OmClearDataAndCommit(deviceId);                                                   // Clear data and commit

            validData = false;
            hasChanged = true;
            om.OnChanged(new OmDeviceEventArgs(this));
            return !failed;
        }

    }
}
