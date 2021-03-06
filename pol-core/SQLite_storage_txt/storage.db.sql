BEGIN TRANSACTION;
DROP TABLE IF EXISTS "Item";
CREATE TABLE IF NOT EXISTS "Item" (
	"ItemId"	INTEGER NOT NULL,
	"StorageAreaId"	INTEGER NOT NULL,
	"Name"	TEXT,
	"Serial"	INTEGER NOT NULL,
	"ObjType"	INTEGER NOT NULL,
	"Graphic"	INTEGER NOT NULL,
	"Color"	INTEGER,
	"X"	INTEGER NOT NULL,
	"Y"	INTEGER NOT NULL,
	"Z"	INTEGER NOT NULL,
	"Facing"	INTEGER,
	"Revision"	INTEGER NOT NULL,
	"Realm"	TEXT NOT NULL,
	"CPropId"	INTEGER,
	"Amount"	INTEGER,
	"Layer"	INTEGER,
	"Movable"	INTEGER,
	"Invisible"	INTEGER,
	"Container"	INTEGER,
	"OnUseScript"	TEXT,
	"EquipScript"	TEXT,
	"UnequipScript"	TEXT,
	"DecayAt"	INTEGER,
	"SellPrice"	INTEGER,
	"BuyPrice"	INTEGER,
	"Newbie"	INTEGER,
	"Insured"	INTEGER,
	"FireResist"	INTEGER,
	"ColdResist"	INTEGER,
	"EnergyResist"	INTEGER,
	"PoisonResist"	INTEGER,
	"PhysicalResist"	INTEGER,
	"FireDamage"	INTEGER,
	"ColdDamage"	INTEGER,
	"EnergyDamage"	INTEGER,
	"PoisonDamage"	INTEGER,
	"PhysicalDamage"	INTEGER,
	"LowerReagentCost"	INTEGER,
	"SpellDamageIncrease"	INTEGER,
	"FasterCasting"	INTEGER,
	"FasterCastRecovery"	INTEGER,
	"DefenceIncrease"	INTEGER,
	"DefenceIncreaseCap"	INTEGER,
	"LowerManaCost"	INTEGER,
	"HitChance"	INTEGER,
	"FireResistCap"	INTEGER,
	"ColdResistCap"	INTEGER,
	"EnergyResistCap"	INTEGER,
	"PhysicalResistCap"	INTEGER,
	"PoisonResistCap"	INTEGER,
	"Luck"	INTEGER,
	"MaxHp_mod"	INTEGER,
	"Hp"	INTEGER,
	"Quality"	INTEGER,
	"NameSuffix"	TEXT,
	"NoDrop"	INTEGER,
	"FireResistMod"	INTEGER,
	"ColdResistMod"	INTEGER,
	"EnergyResistMod"	INTEGER,
	"PoisonResistMod"	INTEGER,
	"PhysicalResistMod"	INTEGER,
	"FireDamageMod"	INTEGER,
	"ColdDamageMod"	INTEGER,
	"EnergyDamageMod"	INTEGER,
	"PoisonDamageMod"	INTEGER,
	"PhysicalDamageMod"	INTEGER,
	"LowerReagentCostMod"	INTEGER,
	"DefenceIncreaseMod"	INTEGER,
	"DefenceIncreaseCapMod"	INTEGER,
	"LowerManaCostMod"	INTEGER,
	"HitChanceMod"	INTEGER,
	"FireResistCapMod"	INTEGER,
	"ColdResistCapMod"	INTEGER,
	"EnergyResistCapMod"	INTEGER,
	"PhysicalResistCapMod"	INTEGER,
	"PoisonResistCapMod"	INTEGER,
	"SpellDamageIncreaseMod"	INTEGER,
	"FasterCastingMod"	INTEGER,
	"FasterCastRecoveryMod"	INTEGER,
	"LuckMod"	INTEGER,
	PRIMARY KEY("ItemId"),
	FOREIGN KEY("StorageAreaId") REFERENCES "StorageArea"("StorageAreaId") ON UPDATE CASCADE ON DELETE CASCADE
);
DROP TABLE IF EXISTS "CProp";
CREATE TABLE IF NOT EXISTS "CProp" (
	"CPropId"	INTEGER NOT NULL,
	"PropName"	TEXT,
	"Value"	TEXT,
	"ItemId"	INTEGER NOT NULL,
	PRIMARY KEY("CPropId"),
	FOREIGN KEY("ItemId") REFERENCES "Item"("ItemId") ON UPDATE CASCADE ON DELETE CASCADE
);
DROP TABLE IF EXISTS "StorageArea";
CREATE TABLE IF NOT EXISTS "StorageArea" (
	"StorageAreaId"	INTEGER NOT NULL,
	"Name"	TEXT NOT NULL,
	PRIMARY KEY("StorageAreaId")
);
DROP INDEX IF EXISTS "ItemName";
CREATE INDEX IF NOT EXISTS "ItemName" ON "Item" (
	"Name"	ASC
);
COMMIT;
