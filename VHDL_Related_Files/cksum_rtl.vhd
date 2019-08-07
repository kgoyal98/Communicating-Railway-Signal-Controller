----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date:    15:10:44 04/03/2018 
-- Design Name: 
-- Module Name:    project - Behavioral 
-- Project Name: 
-- Target Devices: 
-- Tool versions: 
-- Description: 
--
-- Dependencies: 
--
-- Revision: 
-- Revision 0.01 - File Created
-- Additional Comments: 
--
----------------------------------------------------------------------------------
library IEEE;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.numeric_std.all;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx primitives in this code.
--library UNISIM;
--use UNISIM.VComponents.all;


architecture rtl of swled is

signal flags : std_logic_vector(3 downto 0);


constant i : integer := 0;
constant coord : std_logic_vector(31 downto 0) := "00000000000000000000000000010010";
constant ack1 : std_logic_vector(31 downto 0) :=  "00000000000000000000000100010010";
constant ack2 : std_logic_vector(31 downto 0) :=  "00000000000000000000000100010010";
constant key : std_logic_vector(31 downto 0) :=   "00000000000000000000000000001111";
constant special1 : std_logic_vector(23 downto 0) := "000000000000000000000001";
constant second : std_logic_vector(33 downto 0) := "0000000010110111000110110000000000";
constant t0 : std_logic_vector(33 downto 0) := "0000011100100111000011100000000000"; -- 10 seconds
constant t256 : std_logic_vector(33 downto 0) := "1011011100011011000000000000000000"; -- 256 seconds


signal enc_output : std_logic_vector(31 downto 0) := (others => '0');
signal enc_input : std_logic_vector(31 downto 0) := (others => '0');
signal enc_input_next : std_logic_vector(31 downto 0) := (others => '0');

signal state : integer range 0 to 40 := 35;
signal state_next : integer range 0 to 40 := 35;
signal dec_input : std_logic_vector(31 downto 0) := (others => '0');
signal dec_input_next : std_logic_vector(31 downto 0) := (others => '0');
signal dec_output : std_logic_vector(31 downto 0) := (others => '0');

signal counter : std_logic_vector(33 downto 0) := (others => '0');
signal counter_next : std_logic_vector(33 downto 0) := (others => '0');

signal enc_reset : std_logic := '1';
signal enc_reset_next : std_logic := '1';
signal enc_enable : std_logic := '0';
signal enc_enable_next : std_logic := '0';
signal enc_done : std_logic := '0';

signal dec_reset : std_logic := '1';
signal dec_reset_next : std_logic := '1';
signal dec_enable : std_logic := '0';
signal dec_enable_next : std_logic := '0';
signal dec_done : std_logic := '0';

signal TrackExists, TrackOK : std_logic_vector(7 downto 0);
signal TrackExists_next, TrackOK_next : std_logic_vector(7 downto 0);
signal NextSignal : std_logic_vector(7 downto 0) := (others => '0');
signal NextSignal_next : std_logic_vector(7 downto 0) := (others => '0');
signal red, amber, green : std_logic := '0';
signal Direction : std_logic_vector(2 downto 0) := "000";
signal Direction_next : std_logic_vector(2 downto 0) := "000";
signal data : std_logic_vector(7 downto 0) := "00000000";
signal data_next : std_logic_vector(7 downto 0) := "00000000";
signal input_done : std_logic_vector(7 downto 0) := "00000000";
signal input_done_next : std_logic_vector(7 downto 0) := "00000000";
signal flag : std_logic :='0';
signal flag_next : std_logic :='0';
signal switch : std_logic_vector(7 downto 0) := "00000000";
signal switch_next : std_logic_vector(7 downto 0) := "00000000";
signal uart_reset : std_logic := '0';
signal txstart : std_logic := '0';
signal txstart_next : std_logic := '0';
signal sample : std_logic;
signal txdata : std_logic_vector(7 downto 0);
signal txdata_next : std_logic_vector(7 downto 0);
signal txdone : std_logic;
signal rxdata : std_logic_vector(7 downto 0);
signal rxdone : std_logic;

signal left_button_deb : std_logic;
signal right_button_deb : std_logic;
signal up_button_deb : std_logic;
signal down_button_deb : std_logic;

signal temp : std_logic_vector(7 downto 0);
signal temp_next : std_logic_vector(7 downto 0);
signal uart_data_received : std_logic := '0';
signal uart_data_received_next : std_logic := '0';

component decrypter
port(
	clock : in  STD_LOGIC;
	K : in  STD_LOGIC_VECTOR (31 downto 0);
	C : in  STD_LOGIC_VECTOR (31 downto 0);
	P : out  STD_LOGIC_VECTOR (31 downto 0);
	reset : in  STD_LOGIC;
	enable : in  STD_LOGIC;
	done: out STD_LOGIC);
end component;

component encrypter
port(
	clock : in  STD_LOGIC;
	K : in  STD_LOGIC_VECTOR (31 downto 0);
	P : in  STD_LOGIC_VECTOR (31 downto 0);
	C : out  STD_LOGIC_VECTOR (31 downto 0);
	reset : in  STD_LOGIC;
	enable : in  STD_LOGIC;
	done: out STD_LOGIC);
end component;

component uart_tx
port (clk	  : in std_logic;
		rst     : in std_logic;
		txstart : in std_logic;
		sample  : in std_logic;
		txdata  : in std_logic_vector(7 downto 0);
		txdone  : out std_logic;
		tx	     : out std_logic);
end component;

component uart_rx
port (clk	: in std_logic;
		rst	: in std_logic;
		rx		: in std_logic;
		sample: in STD_LOGIC;
		rxdone: out std_logic;
		rxdata: out std_logic_vector(7 downto 0));
end component;

component baudrate_gen is
port (clk	: in std_logic;
		rst	: in std_logic;
		sample: out std_logic);
end component baudrate_gen;

component debouncer is
Generic (wait_cycles : STD_LOGIC_VECTOR (19 downto 0) := x"F423F");
Port ( clk : in  STD_LOGIC;
       button : in  STD_LOGIC;
       button_deb : out  STD_LOGIC);
end component;

begin

	enc : encrypter port map (
		clock => clk_in,
		k => key,
		c => enc_output,
		p => enc_input,
		reset => enc_reset,
		enable => enc_enable,
		done => enc_done
		);

	dec : decrypter port map (
		clock => clk_in,
		k => key,
		c => dec_input,
		p => dec_output,
		reset => dec_reset,
		enable => dec_enable,
		done => dec_done
		);

	u_tx : uart_tx port map (
		clk => clk_in,
		rst => uart_reset,
		txstart => txstart,
		sample => sample,
		txdata => txdata,
		txdone => txdone,
		tx => tx
		);

	u_rx : uart_rx port map(
		clk => clk_in,
		rst => uart_reset,
		rx => rx,
		sample => sample,
		rxdone => rxdone,
		rxdata => rxdata
		);

	i_brg : baudrate_gen port map (
		clk => clk_in, 
		rst => uart_reset, 
		sample => sample);

	left_btn : debouncer port map (
		clk => clk_in,
		button => left_button,
		button_deb => left_button_deb);

	right_btn : debouncer port map (
		clk => clk_in,
		button => right_button,
		button_deb => right_button_deb);

	up_btn : debouncer port map (
		clk => clk_in,
		button => up_button,
		button_deb => up_button_deb);

	down_btn : debouncer port map (
		clk => clk_in,
		button => down_button,
		button_deb => down_button_deb);

	clk_update : process(clk_in, reset_in)
	begin
		if (reset_in = '1') then
			-----
			state <= 35;
			counter <= (others => '0');
			uart_reset <= '1';
			enc_reset <= '1';
			dec_reset <= '1';
			uart_data_received <= '0';
			Direction <= "000";
		elsif (rising_edge(clk_in)) then
			-----
			uart_reset <= '0';
			state <= state_next;
			counter <= counter_next;
			enc_enable <= enc_enable_next;
			dec_enable <= dec_enable_next;
			enc_input <= enc_input_next;
			dec_input <= dec_input_next;
			enc_reset <= enc_reset_next;
			dec_reset <= dec_reset_next;

			TrackExists <= TrackExists_next;
			TrackOK <= TrackOK_next;
			NextSignal <= NextSignal_next;
			data <= data_next;
			input_done <= input_done_next;
			Direction <= Direction_next;
			switch <= switch_next;
			flag <= flag_next;
			
			txdata <= txdata_next;
			txstart <= txstart_next;
			temp <= temp_next;
			uart_data_received <= uart_data_received_next;
		end if;
	end process;
			
	next_state : process(state, enc_done, f2hReady_in, counter, dec_done, chanAddr_in, h2fData_in, dec_output, h2fValid_in, enc_enable, enc_input, dec_enable, dec_input, Direction, sw_in, TrackOK, TrackExists, NextSignal, data, switch, flag, up_button_deb, down_button_deb, left_button_deb, right_button_deb, txdone, rxdata, rx, rxdone, txdata, txstart, uart_data_received, temp)
	begin
		-----
		state_next <= state;
		counter_next <= counter;
		enc_enable_next <= enc_enable;
		dec_enable_next <= dec_enable;
		enc_reset_next <= '0';
		dec_reset_next <= '0';
		enc_input_next <= enc_input;
		dec_input_next <= dec_input;
		flag_next <=flag;
		data_next <= data;
		switch_next <= switch;

		Direction_next <= Direction;
		red <= '0';
		amber <= '0';
		green <= '0';
		
		txdata_next <= txdata;
		txstart_next <= txstart;
		
		
		------------------- Macrostate S1
		if (state = 35) then
			if (counter = "0000001000100101010100010000000000") then
				counter_next <= (others => '0');
				state_next <= 0;
				enc_reset_next <= '1';
				dec_reset_next <= '1';
				dec_enable_next <= '0';
			else 
				counter_next <= counter + 1;
			end if;
		elsif (state = 0) then
			enc_enable_next <= '1';
			enc_input_next <= coord;
			enc_reset_next <= '0';
			state_next <= 1;
		-------------------- Macrostate S2
		elsif (state = 1 and enc_done = '1') then
			state_next <= 2;
			counter_next <= "0000000000000000000000000000000011";
		elsif (state = 2 and f2hReady_in = '1' and to_integer(unsigned(chanAddr_in)) = (2 * i)) then
			if (counter = 0) then 
				state_next <= 3;
				counter_next <= (others => '0');
			else 
				counter_next <= counter - 1;
			end if;
		elsif (state = 3) then
			enc_reset_next <= '1';
			enc_enable_next <= '0';
			if (counter = t256) then
				counter_next <= (others => '0');
				state_next <= 0;
			else
				if (to_integer(unsigned(chanAddr_in)) = ((2 * i) + 1) and h2fValid_in = '1') then
					state_next <= 4;
					dec_input_next(31 downto 24) <= h2fData_in;
					counter_next <= "0000000000000000000000000000000010";
				else 
					counter_next <= counter + 1;
				end if;
			end if;

		elsif (state = 4) then 
			if (counter = 0) then
				dec_enable_next <= '1';
				if (dec_done = '1') then	
					state_next <= 5;
				else 
					--------------
				end if;
			else 
				counter_next <= counter - 1;
			end if;
			if (to_integer(unsigned(chanAddr_in)) = ((2 * i) + 1) and h2fValid_in = '1') then
				dec_input_next(((to_integer(unsigned(counter)) * 8) + 7) downto (to_integer(unsigned(counter)) * 8)) <= h2fData_in;
			else 
				--------------------
			end if;

		elsif (state = 5) then
			if (dec_output = coord) then
				state_next <= 6;
				enc_enable_next <= '1';
				enc_input_next <= ack1;
				dec_enable_next <= '0';
				dec_reset_next <='1';
			else 
				----------------
			end if;
		elsif (state = 6) then
			if(enc_done = '1') then
				state_next <= 7;
				counter_next <= "0000000000000000000000000000000011";
			else 
				----------------------
			end if;
		elsif (state = 7 and f2hReady_in = '1' and to_integer(unsigned(chanAddr_in)) = (2 * i)) then
			if (counter = 0) then 
				state_next <= 8;
			else 
				counter_next <= counter - 1;
			end if;
		elsif (state = 8) then

			enc_enable_next <='0';
			enc_reset_next <= '1';

			if (counter = t256) then
				counter_next <= (others => '0');
				state_next <= 0;
			else
				if (to_integer(unsigned(chanAddr_in)) = ((2 * i) + 1) and h2fValid_in = '1') then
					state_next <= 9;
					dec_input_next(31 downto 24) <= h2fData_in;
					counter_next <= "0000000000000000000000000000000010";
				else 
					counter_next <= counter + 1;
				end if;
			end if;
		elsif (state = 9) then 
			if (counter = "0000000000000000000000000000000000") then
				dec_enable_next <= '1';
				if (dec_done = '1') then	
					state_next <= 10;
				else 
					----------------
				end if;
			else 
				counter_next <= counter - 1;
			end if;
			if (to_integer(unsigned(chanAddr_in)) = ((2 * i) + 1) and h2fValid_in = '1') then
				dec_input_next(((to_integer(unsigned(counter)) * 8) + 7) downto (to_integer(unsigned(counter)) * 8)) <= h2fData_in;
			else 
				-----------------
			end if;
		elsif (state = 10) then
			if (dec_output = ack2) then
				state_next <= 11;
				dec_enable_next <= '0';
				dec_reset_next <='1';
				counter_next <= "0000000000000000000000000000000011";
			else 
				----------------
			end if;
		elsif (state = 11) then 
			if (to_integer(unsigned(chanAddr_in)) = ((2 * i) + 1) and h2fValid_in = '1') then
				if (counter = "0000000000000000000000000000000000") then
					dec_reset_next <= '0';
					dec_enable_next <= '1';
					
				else 
					counter_next <= counter - 1;
				end if;
				dec_input_next(((to_integer(unsigned(counter)) * 8) + 7) downto (to_integer(unsigned(counter)) * 8)) <= h2fData_in;
			else 
				-----------------
			end if;
			if (dec_done = '1') then	
				state_next <= 12;
				counter_next <= (others => '0');
			else 
				----------------
			end if;
		elsif (state = 12) then
			if(counter = "0000000000000000000000000000000011") then
				state_next <=13;
				dec_enable_next <='0';
				dec_reset_next <= '1';
				counter_next <= (others => '0');
				enc_enable_next <= '1';
				enc_input_next <= ack1;
			else
				counter_next <= counter+1;
			end if;
			flag_next <='1';
			data_next <= dec_output(((to_integer(unsigned(counter)) * 8) + 7) downto (to_integer(unsigned(counter)) * 8));
		elsif (state = 13) then
			flag_next <='0';
			if(enc_done = '1') then
				state_next <= 14;
				counter_next <= "0000000000000000000000000000000011";
			else 
				----------------------
			end if;
		elsif (state = 14 and f2hReady_in = '1' and to_integer(unsigned(chanAddr_in)) = (2 * i)) then
			if (counter = "0000000000000000000000000000000000") then 
				state_next <= 15;
				counter_next <= "0000000000000000000000000000000011";
			else 
				counter_next <= counter - 1;
			end if;
		elsif (state = 15) then 
			enc_enable_next <='0';
			enc_reset_next <= '1';
			if (to_integer(unsigned(chanAddr_in)) = ((2 * i) + 1) and h2fValid_in = '1') then
				if (counter = "0000000000000000000000000000000000") then
					dec_enable_next <= '1';
				else 
					counter_next <= counter - 1;
				end if;
				dec_input_next(((to_integer(unsigned(counter)) * 8) + 7) downto (to_integer(unsigned(counter)) * 8)) <= h2fData_in;
			else 
				-----------------
			end if;
			if (dec_done = '1') then	
				state_next <= 16;
				counter_next <= (others => '0');
			else 
				----------------
			end if;
		elsif (state = 16) then
			if(counter = "0000000000000000000000000000000011") then
				state_next <=17;
				dec_enable_next <='0';
				dec_reset_next <= '1';
				counter_next <= (others => '0');
				enc_enable_next <= '1';
				enc_input_next <= ack1;
			else
				counter_next <= counter+1;
			end if;
			flag_next <= '1';
			data_next <= dec_output(((to_integer(unsigned(counter)) * 8) + 7) downto (to_integer(unsigned(counter)) * 8));
		elsif (state = 17) then
			flag_next <='0';
			if(enc_done = '1') then
				state_next <= 18;
				counter_next <= "0000000000000000000000000000000011";
			else 
				----------------------
			end if;
		elsif (state = 18 and f2hReady_in = '1' and to_integer(unsigned(chanAddr_in)) = (2 * i)) then
			if (counter = "0000000000000000000000000000000000") then 
				state_next <= 19;
				counter_next <= "0000000000000000000000000000000000";
				enc_enable_next <='0';
				enc_reset_next <= '1';
			else 
				counter_next <= counter - 1;
			end if;
		elsif (state = 19) then
			if (counter = t256) then
				counter_next <= (others => '0');
				state_next <= 0;
			else
				if (to_integer(unsigned(chanAddr_in)) = ((2 * i) + 1) and h2fValid_in = '1') then
					state_next <= 20;
					dec_input_next(31 downto 24) <= h2fData_in;
					counter_next <= "0000000000000000000000000000000010";
				else 
					counter_next <= counter + 1;
				end if;
			end if;
		elsif (state = 20) then 
			if (to_integer(unsigned(chanAddr_in)) = ((2 * i) + 1) and h2fValid_in = '1') then
				if (counter = "0000000000000000000000000000000000") then
					dec_enable_next <= '1';
					
				else 
					counter_next <= counter - 1;
				end if;
				dec_input_next(((to_integer(unsigned(counter)) * 8) + 7) downto (to_integer(unsigned(counter)) * 8)) <= h2fData_in;
			else 
				-----------------
			end if;
			if (dec_done = '1') then
				state_next <= 21;
			else 
				----------------
			end if;
		elsif (state = 21) then
			if (dec_output = ack2) then
				state_next <= 22;
				dec_enable_next <= '0';
				dec_reset_next <='1';
				counter_next <= "0000000000000000000000000000000000";
			else 
				-------------
			end if;
		elsif(state = 22) then
			switch_next <= sw_in;
			state_next <= 23;
			counter_next <= (others => '0');
			Direction_next <= "000";
		elsif(state = 23) then
			if (counter = "0000001000100101010100010000000000") then
				if (Direction = "111") then
					state_next <= 24;
					Direction_next <= "000";
					counter_next <= (others => '0');
				else 
					Direction_next <= Direction + 1;
					counter_next <= (others => '0');
				end if;
			else 
				counter_next <= counter + 1;
				if (TrackExists(to_integer(unsigned(Direction))) = '1') then
					if (TrackOK(to_integer(unsigned(Direction))) = '1') then
						if (switch(to_integer(unsigned(Direction))) = '1') then
							if (switch((to_integer(unsigned(Direction)) + 4) mod 8) = '0' or TrackExists((to_integer(unsigned(Direction)) + 4) mod 8) = '0' or TrackOK((to_integer(unsigned(Direction)) + 4) mod 8) = '0') then
								if (NextSignal(to_integer(unsigned(Direction))) = '1') then
									amber <= '1';
								else 
									green <= '1';
								end if;
							else
								if (to_integer(unsigned(Direction)) > ((to_integer(unsigned(Direction)) + 4) mod 8)) then
									if (counter < "0000000010110111000110110000000000") then
										green <= '1';
									elsif (counter < "0000000101101110001101100000000000") then
										amber <= '1';
									else 
										red <= '1';
									end if;
								else 
									red <= '1';
								end if;
							end if;
						else 
							red <= '1';
						end if;
					else 
						red <= '1';
					end if;
				else 
					red <= '1';
				end if;
			end if;
		------------------ Macrostate S3
		elsif (state = 24) then
			if (up_button_deb = '1') then
				state_next <= 25;
			else
				state_next <= 30;
			end if;
		elsif (state = 25) then
			switch_next <= sw_in;
			if (down_button_deb = '1') then
				state_next <= 26;
			else 
				----------------
			end if;
		elsif (state = 26) then
			enc_input_next <= special1 & switch;
			enc_enable_next <= '1';
			enc_reset_next <= '0';
			state_next <= 27;
		elsif (state = 27) then
			if (enc_done = '1') then
				counter_next <= "0000000000000000000000000000000011";
				state_next <= 28;
				if (left_button_deb = '1') then
					state_next <= 28;
				else 
					state_next <= 29;
				end if;
			else
				-------------------
			end if;
		elsif (state = 28 and f2hReady_in = '1' and to_integer(unsigned(chanAddr_in)) = (2 * i)) then
			if (counter = "0000000000000000000000000000000000") then
				state_next <= 31;
				enc_reset_next <= '1';
				enc_enable_next <= '0';
			else
				counter_next <= counter - 1;
			end if;
		elsif (state = 29 and f2hReady_in = '1' and to_integer(unsigned(chanAddr_in)) = (2 * i)) then
			if (counter = "0000000000000000000000000000000000") then
				state_next <= 34;
				enc_reset_next <= '1';
				enc_enable_next <= '0';
			else
				counter_next <= counter - 1;
			end if;
		----------------- Macrostate S4
		elsif (state = 30) then
			if (left_button_deb = '1') then
				state_next <= 31;
			else 
				state_next <= 34;
			end if;
		elsif (state = 31) then
			switch_next <= sw_in;
			if (right_button_deb = '1') then
				state_next <= 32;
			else 
				---------------
			end if;
		elsif (state = 32) then
			if(txdone = '1') then
				txdata_next <= switch;
				txstart_next <= '1';
				state_next <= 33;
			else
				----------------
			end if;
		elsif (state = 33) then
			if (txdone = '1') then
				state_next <= 34;
				txstart_next <= '0';
			else
				----------------
			end if;

		------------------Macrostate S5
		elsif (state = 34) then
			if (uart_data_received = '1') then
				state_next <= 36;
			else
				state_next <= 36;
			end if;
		------------------- Macrostate S6
		elsif (state = 36) then
			if (counter = t0) then --- Wait for 5 seconds
				counter_next <= (others => '0');
				state_next <= 0;
			else 
				counter_next <= counter + 1;
			end if;
		else
			---------------------
		end if;
	end process;

data1: process(data,counter,flag, input_done, TrackExists, TrackOK, NextSignal, uart_data_received, temp)
begin
	input_done_next  <= input_done;
	TrackExists_next <= TrackExists;
	TrackOK_next <= TrackOK;
	NextSignal_next <= NextSignal;
	if(counter = "1011011100011011000000000000000000") then
		input_done_next <= (others => '0');
	elsif(flag = '1') then
		if(uart_data_received = '1' and (temp(7 downto 5) = data(7 downto 5)) and data(4) = '1') then
			--if((to_integer(unsigned(data(2 downto 0)))=0) or (to_integer(unsigned(data(2 downto 0)))=1)) then
			if((to_integer(unsigned(data(2 downto 0)))=1)) then
			--if (data(2) = '0' and data(1) = '0') then
				if (to_integer(unsigned(data(7 downto 5))) = 0) then
					--if (((to_integer(unsigned(temp(2 downto 0))) = 0) or (to_integer(unsigned(temp(2 downto 0))) = 1))) then
					if (((to_integer(unsigned(temp(2 downto 0))) = 1))) then
						NextSignal_next <= NextSignal(7 downto 1) & '1';
					else 
						NextSignal_next <= NextSignal(7 downto 1) & '0';
					end if;
				elsif (to_integer(unsigned(data(7 downto 5))) = 7) then
					--if (((to_integer(unsigned(temp(2 downto 0))) = 0) or (to_integer(unsigned(temp(2 downto 0))) = 1))) then
					if (((to_integer(unsigned(temp(2 downto 0))) = 1))) then
						NextSignal_next <= '1' & NextSignal(6 downto 0);
					else 
						NextSignal_next <= '0' & NextSignal(6 downto 0);
					end if;
				else
					--if (((to_integer(unsigned(temp(2 downto 0))) = 0) or (to_integer(unsigned(temp(2 downto 0))) = 1))) then
					if (((to_integer(unsigned(temp(2 downto 0))) = 1))) then
						NextSignal_next <= NextSignal(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & '1' & NextSignal((to_integer(unsigned(data(7 downto 5))) - 1) downto 0);
					else 
						NextSignal_next <= NextSignal(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & '0' & NextSignal((to_integer(unsigned(data(7 downto 5))) - 1) downto 0);
					end if;
				end if;
			else 
				if (to_integer(unsigned(data(7 downto 5))) = 0) then
					NextSignal_next <= NextSignal(7 downto 1) & '0';
				elsif (to_integer(unsigned(data(7 downto 5))) = 7) then
					NextSignal_next <= '0' & NextSignal(6 downto 0);
				else
					NextSignal_next <= NextSignal(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & '0' & NextSignal((to_integer(unsigned(data(7 downto 5))) - 1) downto 0);
				end if;
			end if;
		else
			--if((to_integer(unsigned(data(2 downto 0)))=0) or (to_integer(unsigned(data(2 downto 0)))=1)) then
			if((to_integer(unsigned(data(2 downto 0)))=1)) then
				if (to_integer(unsigned(data(7 downto 5))) = 0) then
					NextSignal_next <= NextSignal(7 downto 1) & '1';
				elsif (to_integer(unsigned(data(7 downto 5))) = 7) then
					NextSignal_next <= '1' & NextSignal(6 downto 0);
				else
					NextSignal_next <= NextSignal(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & '1' & NextSignal((to_integer(unsigned(data(7 downto 5))) - 1) downto 0);
				end if;
			else 
				if (to_integer(unsigned(data(7 downto 5))) = 0) then
					NextSignal_next <= NextSignal(7 downto 1) & '0';
				elsif (to_integer(unsigned(data(7 downto 5))) = 7) then
					NextSignal_next <= '0' & NextSignal(6 downto 0);
				else
					NextSignal_next <= NextSignal(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & '0' & NextSignal((to_integer(unsigned(data(7 downto 5))) - 1) downto 0);
				end if;
			end if;
		end if;
		if(uart_data_received = '1' and (temp(7 downto 5) = data(7 downto 5)) and data(4) = '1') then
			if(to_integer(unsigned(data(7 downto 5))) = 0) then
				TrackExists_next <= TrackExists(7 downto 1) & data(4);
				TrackOK_next <= TrackOK(7 downto 1) & (data(3) and temp(3));
				input_done_next <= input_done(7 downto 1) & '1' ;
			elsif (to_integer(unsigned(data(7 downto 5))) = 7) then
				TrackExists_next <= data(4) & TrackExists(6 downto 0) ;
				TrackOK_next <= (data(3) and temp(3)) & TrackOK(6 downto 0) ;
				input_done_next <= '1' & input_done(6 downto 0) ;
			else 
				TrackExists_next <= TrackExists(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & data(4) & TrackExists((to_integer(unsigned(data(7 downto 5))) - 1) downto 0);
				TrackOK_next <= TrackOK(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & (data(3) and temp(3)) & TrackOK((to_integer(unsigned(data(7 downto 5))) - 1) downto 0) ;
				input_done_next <= input_done(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & '1' & input_done((to_integer(unsigned(data(7 downto 5))) - 1) downto 0);
			end if;
		else
			if(to_integer(unsigned(data(7 downto 5))) = 0) then
				TrackExists_next <= TrackExists(7 downto 1) & data(4);
				TrackOK_next <= TrackOK(7 downto 1) & data(3);
				input_done_next <= input_done(7 downto 1) & '1' ;
			elsif (to_integer(unsigned(data(7 downto 5))) = 7) then
				TrackExists_next <= data(4) & TrackExists(6 downto 0) ;
				TrackOK_next <= data(3) & TrackOK(6 downto 0) ;
				input_done_next <= '1' & input_done(6 downto 0) ;
			else 
				TrackExists_next <= TrackExists(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & data(4) & TrackExists((to_integer(unsigned(data(7 downto 5))) - 1) downto 0);
				TrackOK_next <= TrackOK(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & data(3) & TrackOK((to_integer(unsigned(data(7 downto 5))) - 1) downto 0) ;
				input_done_next <= input_done(7 downto (to_integer(unsigned(data(7 downto 5))) + 1)) & '1' & input_done((to_integer(unsigned(data(7 downto 5))) - 1) downto 0);
			end if;
		end if;
	else

	end if;
	
	
end process;

uart_proc : process(rxdone, temp, rxdata)
begin
	temp_next <= temp;
	if (rxdone = '1') then
		temp_next <= rxdata;
	else
	
	end if;
end process;

f2hData_out <= 
	enc_output(((to_integer(unsigned(counter)) * 8) + 7) downto (to_integer(unsigned(counter)) * 8))
		when to_integer(unsigned(chanAddr_in)) = (2 * i) and ((state = 2) or (state = 7) or (state = 14) or (state=18) or (state = 28) or (state = 29))
	else input_done when chanAddr_in = "0000010"
	else switch when chanAddr_in = "0000011"
	else dec_output(7 downto 0) when chanAddr_in = "0000100"
	else dec_input(7 downto 0) when chanAddr_in = "0000101"
	else std_logic_vector(to_unsigned(state, 8)) when chanAddr_in = "0000110"
	else temp when chanAddr_in = "0000111"
	else uart_data_received & "0000000" when chanAddr_in = "0001000"
	else x"00";

f2hValid_out <= '1' when ((state = 2) or (state = 7) or (state = 14) or (state=18) or (state = 28) or (state = 29))
	else '0';

h2fReady_out <= '1';

uart_data_received_next <= '1' when rxdone = '1'
	else '0' when state = 18
	else uart_data_received;

led_out <= "11111111" when state = 35
	else Direction & "00" & green & amber & red;


flags <= "00" & f2hReady_in & reset_in;
	seven_seg : entity work.seven_seg
		port map(
			clk_in     => clk_in,
			data_in    => (others => '0'),
			dots_in    => flags,
			segs_out   => sseg_out,
			anodes_out => anode_out
		);

end architecture;


