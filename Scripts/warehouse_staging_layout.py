"""Shared dock-side staging poses, in centimetres; no editor side effects."""
INBOUND = (1100, -1640, 400, 310)
OUTBOUND = (-1000, -1640, 350, 310)
STAGING_ZONES = (INBOUND, OUTBOUND)
PALLET_POSES = {
    'WH_TrainingPallet': (1300, -1790, .1),
    'WH_TrainingPallet_02': (1300, -1537.5, .1),
    'WH_InboundPallet_Empty': (830, -1350, .1),
}
VEHICLE_POSES = {
    'WH_AutonomousForklift': (1030, -1537.5, 0),
    'WH_AutonomousForklift_02': (1030, -1790, 0),
}
OUTBOUND_PALLETS = {
    'WH_OutboundPallet_Empty_01': (-1000, -1395, .1),
    'WH_OutboundPallet_Empty_02': (-1190, -1395, .1),
}
RELOCATED_POSES = {
    'WH_Drop_Pallet': (-1000, -1780, .1),
    'WH_ChargingStation': (2250, -800, 0),
    'WH_ChargingStation_02': (2250, -300, 0),
    'WH_Worker_Maintenance': (2500, -650, 96.5),
    'WH_Worker_Receiving': (1530, -1180, 96.5),
    'WH_Worker_Packing': (-1230, -1180, 96.5),
    'WH_Sign_Controls': (2040, -600, 200),
}
NAVIGATION_ANCHORS = [
    (1030, -1790, 0), (1030, -1537.5, 0),
    (1030, -1790, 90), (1030, -1625, 90),
    (1550, -1625, 0), (2100, -1625, 0),
    (2400, -1662.5, 0), (2400, -1537.5, 0),
    (2225, -1300, 90), (2250, -640, 90), (2250, -140, 90),
    (2250, 300, 90), (2110, -1000, -90),
    (900, -1900, 0), (0, -1900, 0),
    (700, -1570, 0), (0, -1570, 0), (-750, -1570, 0),
    (700, -1570, 180), (0, -1570, 180), (-750, -1570, 180),
    (-1220, -1570, -90), (-1220, -1780, -90), (-1220, -1780, 0),
    (-580, -1570, 180), (-580, -1395, 180),
]
