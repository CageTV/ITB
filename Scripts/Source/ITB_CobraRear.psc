ScriptName ITB_CobraRear extends Actor

Armor Property NormalSkin Auto
Armor Property RearedSkin Auto

Event OnCombatStateChanged(Actor akTarget, int aeCombatState)
    if aeCombatState != 0
        UnequipItem(NormalSkin, false, true)
        EquipItem(RearedSkin, false, true)
    else
        UnequipItem(RearedSkin, false, true)
        EquipItem(NormalSkin, false, true)
    endif
EndEvent
