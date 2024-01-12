-- https://forums.desmume.org/viewtopic.php?id=11715

joyid = 0 --change this if your preferred controller isn't being picked up
deadzone = 0.15 --Adjust analog stick deadzones

cooldown=0
weapon=0
subweapon=0
togglestylus=false
toggleaim=false
vcurx=128
vcury=96
key = {}
joy = {}

--Set the controls here
morphkey = "joy.down" --morph ball switch
visorkey = "joy.up" --scan visor switch(hold)
okkey = "joy.2" --ok button in messages
leftkey = "joy.left" --left arrow in messages
rightkey = "joy.right" --right arrow in messages
weaponkey = "joy.4" --weapon cycle
subweaponkey1 = "joy.5" --subweapon cycle back
subweaponkey2 = "joy.6" --subweapon cycle forward
leftstickx = "joy.x" --must be analog stick axis
leftsticky = "joy.y" --must be analog stick axis
rightstickx = "joy.u" --must be analog stick axis
rightsticky = "joy.r" --must be analog stick axis
shootkey = "joy.z" --may be either analog axis or button
boostkey = "joy.3" --morph ball boost
vstyluskey = "joy.7" --virtual stylus(hold)
jumpkey = "joy.1" --jump
startkey = "joy.8" --start


--https://www.lua.org/pil/14.1.html
function getfield (f)
  local v = _G    -- start with the table of globals
  for w in string.gfind(f, "[%w_]+") do
    v = v[w]
  end
  return v
end

while true do
    joysend = {}
    key=input.get()
    joy = controller.get(joyid)
    
    if getfield(vstyluskey) then
        toggleaim = true --this exists to just delay the pressing of the screen when you release the virtual stylus key
        if vcury>0 then
            gui.drawbox(vcurx-1, vcury-1, vcurx+1, vcury+1, "#000000", "#FFFFFF")
        else
            gui.drawbox(vcurx-1, vcury, vcurx+1, vcury+1, "#FFFFFF") --workaround for vcury=0, as it would draw to the top screen
            gui.pixel(vcurx, vcury, "#000000")
        end
        
        if getfield(subweaponkey2) then
            stylus.set{x=vcurx, y=vcury, touch=true}
        end
        if getfield(subweaponkey1) and cooldown==0 then
            togglestylus = not togglestylus
            print('Touchscreen input allowed = ' .. tostring(togglestylus))
            cooldown = 20
        end
        tmp = getfield(rightstickx)
        if tmp and math.abs(tmp)>deadzone then
            vcurx = vcurx + (tmp * 5)
        end
        tmp = getfield(rightsticky)
        if tmp and math.abs(tmp)>deadzone then
            vcury = vcury + (tmp * 5)
        end
        
        if vcurx<0 then vcurx=0 end
        if vcurx>255 then vcurx=255 end
        if vcury<0 then vcury=0 end
        if vcury>191 then vcury=191 end
    else
        if getfield(morphkey) then --Morph
            stylus.set{touch=false} emu.frameadvance() emu.frameadvance()
            stylus.set{x=231, y=167, touch=true} emu.frameadvance() emu.frameadvance()
        end
        if getfield(visorkey) then --Visor
            if cooldown == 0 then
                stylus.set{touch=false} emu.frameadvance() emu.frameadvance()
            end
            cooldown = 10
            stylus.set{x=128, y=173, touch=true}
        end
        if getfield(okkey) then --OK (in scans and messages)
            stylus.set{touch=false} emu.frameadvance() emu.frameadvance()
            stylus.set{x=128, y=142, touch=true} emu.frameadvance() emu.frameadvance()
        end
        if getfield(leftkey) then --Left arrow (in scans and messages)
            stylus.set{touch=false} emu.frameadvance() emu.frameadvance()
            stylus.set{x=71, y=141, touch=true} emu.frameadvance() emu.frameadvance()
        end
        if getfield(rightkey) then --Right arrow (in scans and messages)
            stylus.set{touch=false} emu.frameadvance() emu.frameadvance()
            stylus.set{x=185, y=141, touch=true} emu.frameadvance() emu.frameadvance()
        end
        if getfield(weaponkey) and cooldown==0 then --Switch weapon (beam->missile->subweapon->beam)
            cooldown=20
            weapon=(weapon+1)%3
            stylus.set{touch=false} emu.frameadvance() emu.frameadvance()
            stylus.set{x=85+40*weapon, y=32, touch=true} emu.frameadvance()
            stylus.set{x=85+40*weapon, y=32, touch=true}
        end
        if (getfield(subweaponkey1) or getfield(subweaponkey2)) and cooldown==0 then --Switch subweapon (previous and next)
            cooldown=20
            weapon=2
            if getfield(subweaponkey1) then subweapon=(subweapon-1)%6 end --previous
            if getfield(subweaponkey2) then subweapon=(subweapon+1)%6 end --next
            subX=93+25*subweapon subY=48+25*subweapon
            stylus.set{touch=false} emu.frameadvance() emu.frameadvance()
            stylus.set{x=232, y=34, touch=true} emu.frameadvance()
            stylus.set{x=232, y=34, touch=true} emu.frameadvance()
            stylus.set{x=subX, y=subY, touch=true} emu.frameadvance()
            stylus.set{x=subX, y=subY, touch=true}
        end
        tmp = getfield(leftstickx)
        if tmp and tmp < -deadzone then
            joysend.left = true
        elseif tmp and tmp > deadzone then
            joysend.right = true
        end
        tmp = getfield(leftsticky)
        if tmp and tmp < -deadzone then
            joysend.up = true
        elseif tmp and tmp > deadzone then
            joysend.down = true
        end
        tmp = getfield(rightstickx)
        if tmp and math.abs(tmp)>deadzone and togglestylus==false then
            memory.writedword(0x020DE526, (tmp * 4))
            toggleaim=false
        end
        tmp = getfield(rightsticky)
        if tmp and math.abs(tmp)>deadzone and togglestylus==false then
            memory.writedword(0x020DE52E, (tmp * 6))
            toggleaim=false
        end
        if getfield(boostkey) then
            joysend.R = true
        end
        
        tmp = getfield(shootkey)
        if tmp then
            if type(tmp) == "number" then
                if math.abs(tmp) > deadzone then
                    joysend.L = true
                end
            elseif type(tmp) == "boolean" then
                joysend.L = true
            end
        end
        
        if getfield(jumpkey) then
            joysend.B = true
        end
        if getfield(startkey) then
            joysend.start = true
        end
    end
    
    if cooldown>0 then
        cooldown=cooldown-1
    else
        ball = memory.readbyte(0x020DA818) == 0x02 --Is this a good way of detecting morph ball status??
        if ball==false and togglestylus==false and toggleaim==false then
            stylus.set{x=128, y=96, touch=true} --Required for aiming
        end
    end
    
    joypad.set(1,joysend)
    emu.frameadvance()
end