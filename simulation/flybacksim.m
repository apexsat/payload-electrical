% LM5022 Datasheet: https://www.ti.com/lit/ds/symlink/lm5022.pdf
% LM5022 Flyback reference design: https://www.ti.com/lit/ug/tiduco4/tiduco4.pdf

%Simulation time steps
t_end = 1;
t_step = t_end/1e4;

%Design details
Cload = 200e-6; % Output capacitance
Rload = 1000e3; % Bleed resistor for Steady State calcs
Vf = 1.2; % Output Diode Forward Voltage
Vload_max = 400; % Output regulation voltage limit
N12 = 1/20; % Transformer N1:N2
Vin = 16.8; % 12 < Vin < 16.8
fsw = 250e3; % 250khz
Ripple_ratio = 0.03; % 3% Vin ripple ratio
Lpri = 5.1e-6;
Lpri_leakage = 0.24e-6;
Lsec = 2e-3;

% Change Rsns, Rs2, and Rs1 to change the current limit
Rsns = 50e-3; % Primary side switch current sense resistor
Rs2 = 10e3; % Rs2 and Rs1 are as referred to in LM5022 datasheet. 
Rs1 = 100;

%LM5022 params
Dmax = 0.9; % Maximum duty cycle
Vcl = 0.5; % Current limit thresh
Icl = 45e-6; % Current limit injected current
Rcl_int = 2e3; % internal current limit resistance

%GaN FET params
Rdson = 1.8e-3;
Qg = 23e-9;
Coss = 1e-9;
Rgate_tot = 5; % MOSFET Rg + any external gate resistor
Vdrive = 6;


%Simulation outputs
Vload = zeros([t_end / t_step + 1 1]); % Output load voltage
Iload = zeros([t_end / t_step + 1 1]); % Output load current, which equals Isec_avg
Ilim = zeros([t_end / t_step + 1 1]); % Current limit resulting from Rsns, Rs1/Rs2, and current limit comparator
Ipri_pk = zeros([t_end / t_step + 1 1]); % Primary side peak current
Isec_pk = zeros([t_end / t_step + 1 1]); % Secondary side peak current
Ipri_avgpk = zeros([t_end / t_step + 1 1]); % Primary side average current during on time
Isec_avgpk = zeros([t_end / t_step + 1 1]); % Secondary side average current during on time
D = zeros([t_end / t_step + 1 1]); % Switch Duty Cycle

%Intermediate vals for CCM calculation
Lpri_crit = zeros([t_end / t_step + 1 1]);
Lsec_crit = zeros([t_end / t_step + 1 1]);

t_charged = 0;

t = zeros([t_end / t_step + 1 1]);

index = 1;
while (t(index) < t_end - 1e-12) 

    % Duty cycle and switch voltage/current calculations
    D(index + 1) = min((Vload(index) + Vf)*N12 / (Vin + (Vload(index) + Vf)*N12), Dmax);
    Ilim(index + 1) = max((Vcl - (Rs2 + Rs1 + Rcl_int) * Icl * D(index + 1)) / Rsns, 0);
    if (Vload (index) >= Vload_max)
        if (t_charged == 0)
            t_charged = t(index);
        end
        Vload (index + 1) = Vload(index); % Assume perfect DC regulation (I know, I know...)
        Iload(index + 1) = Vload(index + 1) / Rload;
        Isec_avgpk(index + 1) = Iload(index + 1) / (1 - D(index + 1));
        Isec_pk(index + 1) = 2 * Isec_avgpk(index + 1) / (2 - (1 - D(index + 1)));
        Ipri_avgpk(index + 1) = Isec_avgpk(index + 1) / N12;
        Ipri_pk(index + 1) = 2 * Ipri_avgpk(index + 1) / (2 - D(index + 1));
    else
        Ipri_pk(index + 1) = Ilim(index + 1);
        Ipri_avgpk(index + 1) = Ipri_pk(index + 1) * (2 - D(index + 1)) / 2;
        Isec_avgpk(index + 1) = Ipri_avgpk(index + 1) * N12;
        Iload(index + 1) = Isec_avgpk(index + 1) * (1 - D(index + 1));
        Vload(index + 1) = Vload(index) + (Iload(index + 1) - Vload(index) / Rload) * t_step / Cload;
    end

    % CCM calculation
    Lsec_crit(index + 1) = (1 - D(index + 1))^2 * Vload(index + 1) / (2 * Iload(index + 1) * (fsw));
    Lpri_crit(index + 1) = Lsec_crit(index + 1) * (N12^2);
    
    t(index + 1) = t(index) + t_step;
    index = index + 1;
end

Ipri_rms = Ipri_avgpk .* sqrt(D); % RMS primary side current
Isec_rms = Isec_avgpk .* sqrt(1 - D); % RMS secondary side current

% Efficiency calculation
trise = Rgate_tot * Qg / Vdrive; % Assumes one RC constant is good to turn on/off FET
tfall = Rgate_tot * Qg / Vdrive;
Pdiode = Iload * Vf; % Power lost in output diode
Prsns = Ipri_rms.^2 * Rsns; % Power lost in sense resistor
Prload = Vload.^2 / Rload; % Rload in this case is purely a parasitic draw
Prdson = Ipri_rms.^2 * Rdson; % Power lost in FET rdson
Pdrive = fsw * Qg * Vdrive * ones([t_end / t_step + 1 1]); % Power lost in driving FET
Poss = 0.5*Coss * (Vin^2) * fsw * ones([t_end / t_step + 1 1]); % Power lost in output capacitance of FET
Psw = 0.5 * Vin * fsw * (trise * Vdrive / Rgate_tot + tfall * Vdrive / Rgate_tot) * ones([t_end / t_step + 1 1]); 
Psnubber = 0.5 * Lpri_leakage * Ipri_pk.^2 * fsw; % ChatGPT derived snubber power lost
Ptotal = Pdiode + Prsns + Prload + Prdson + Pdrive + Poss + Psw + Psnubber;
Pin = Iload .* Vload; % I know this should be Vin * Iin but you'd have to do Ipri_rms * Vpri_rms and I dont feel like calculating Vpri_rms. Deal with it; assume ideal conversion
eff = max(1 - Ptotal ./ Pin, 0); % Max 0 becomes pin is sometimes zero causing eff to be -inf. lol

disp("Cin(min) (uF):");
disp(1e6*max(Ipri_pk .* D) / (fsw * Vin * Ripple_ratio));
disp("Time to capacitor charge (s): ");
disp(t_charged);
disp("Secondary critical inductance if aiming for always CCM (H): ");
disp(max(Lsec_crit));
disp("Primary critical inductance if aiming for always CCM (H): ");
disp(max(Lpri_crit));
disp("Secondary critical inductance if DCM allowable at steady state (H):");
disp(max(Lsec_crit(1:floor(t_charged / t_step)))); 
disp("Primary critical inductance if DCM allowable at steady state (H):");
disp(max(Lpri_crit(1:floor(t_charged / t_step))));

disp("Average Efficiency:");
disp(1 - sum(Ptotal) / sum(Pin));

figure()
hold on
plot(t, Iload);
xlabel('Time (s)');
ylabel('Current (A)');
title('Output Current');
grid on
hold off

figure()
hold on
plot(t, Ilim);
xlabel('Time (s)');
ylabel('Current (A)');
title('Current Limit');
grid on
hold off

figure()
hold on
plot(t, Ipri_pk);
xlabel('Time (s)');
ylabel('Current (A)');
title('Primary peak current');
grid on
hold off

figure()
hold on
plot(t, Vload);
xlabel('Time (s)');
ylabel('Voltage (V)');
title('Output Voltage');
grid on
hold off

figure()
hold on
plot(t, D);
xlabel('Time (s)');
ylabel('Duty');
title('Duty Cycle');
grid on
hold off

figure()
hold on
plot(t, 1e3*Lpri_crit);
plot(t, 1e3*Lsec_crit);
xlabel('Time (s)');
ylabel('Critical inductance (mH)');
title('Critical inductance for primary and secondary');
grid on
hold off

figure()
hold on
plot(t, eff);
xlabel('Time (s)');
ylabel('Efficiency');
title('Efficiency vs Time');
grid on
hold off

figure()
hold on
plot(t, Pin);
xlabel('Time (s)');
ylabel('Power In (W)');
title('Power In');
grid on
hold off

figure()
hold on
plot(t, Ptotal);
plot(t, Prsns);
plot(t, Prload);
plot(t, Prdson);
plot(t, Pdrive);
plot(t, Poss);
plot(t, Psw);
plot(t, Psnubber);
colororder("glow12");
legend('Ptotal', 'Prsns', 'Prload', 'Prdson', 'Pdrive', 'Poss', 'Psw', 'Psnubber');
xlabel('Time (s)');
ylabel('Total power lost (W)');
title('Power Lost');
grid on
hold off